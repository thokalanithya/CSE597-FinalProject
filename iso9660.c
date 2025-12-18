#include <types.h>
#include <printf.h>
#include "iso9660.h"

#define SECTOR_SIZE 2048
#define PVD_SECTOR 16

static uint32_t ISO_BASE = 0;
static uint32_t ISO_SIZE = 0;

#define ISO_FLAG_DIRECTORY 0x02
#define ISO_MAX_NAME 64
#define ISO_MAX_DEPTH 16

#define ISO_IS_DOT_ENTRY(rec) \
    ((rec)->name_len == 1 && ((rec)->name[0] == 0 || (rec)->name[0] == 1))

#define ISO_IS_DOT_OR_EMPTY(rec) \
    ((rec)->name_len == 0 || \
     ((rec)->name_len == 1 && ((rec)->name[0] == 0 || (rec)->name[0] == 1)))


/*
 * ISO 9660 Directory Record Structure
 */
typedef struct {
    uint8_t length;
    uint8_t ext_attr_length;
    uint32_t extent_lba_le;
    uint32_t extent_lba_be;
    uint32_t data_length_le;
    uint32_t data_length_be;
    uint8_t recording_date[7];
    uint8_t flags;
    uint8_t file_unit_size;
    uint8_t interleave;
    uint16_t vol_seq_le;
    uint16_t vol_seq_be;
    uint8_t name_len;
    char name[];
} __attribute__((packed)) iso_dir_record_t;

/* Forward declarations */
static iso_dir_record_t *iso9660_find_path(const char *path);

void iso9660_init(uint32_t base, uint32_t size)
{
    ISO_BASE = base;
    ISO_SIZE = size;

    printf("ISO9660: initialized at %x (size %u)\n", ISO_BASE, ISO_SIZE);
    uint8_t *iso = (uint8_t *)ISO_BASE;
    printf("[DBG] ISO ID = %.5s\n", &iso[16 * 2048 + 1]);
}

static void clean_filename(const char *name, int len, char *out)
{
    int j = 0;
    for (int i = 0; i < len && j < ISO_MAX_NAME - 1; i++) {
        char c = name[i];
        if (c == ';') break;
        if (c >= 'A' && c <= 'Z')
            c = c + ('a' - 'A');
        out[j++] = c;
    }
    out[j] = '\0';
}


void iso9660_list_root()
{
    uint8_t *pvd = (uint8_t *)(ISO_BASE + PVD_SECTOR * SECTOR_SIZE);
    printf("PVD bytes: %02x %c %c %c %c %c\n",
    pvd[0], pvd[1], pvd[2], pvd[3], pvd[4], pvd[5]);

    if (pvd[0] != 1 ||
        pvd[1] != 'C' || pvd[2] != 'D' ||
        pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1') {

        printf("ISO9660: Invalid Primary Volume Descriptor\n");
        return;
    }


    printf("ISO9660: Primary Volume Descriptor OK\n");

    iso_dir_record_t *root = (iso_dir_record_t *)&pvd[156];

    uint32_t root_lba  = root->extent_lba_le;
    uint32_t root_size = root->data_length_le;

    printf("Root Directory LBA=%u size=%u bytes\n", root_lba, root_size);

    uint8_t *dir = (uint8_t *)(ISO_BASE + root_lba * SECTOR_SIZE);

    uint32_t offset = 0;

    while (offset < root_size) {

        iso_dir_record_t *rec = (iso_dir_record_t *)(dir + offset);

        if (rec->length == 0) {
            offset = (offset + SECTOR_SIZE) & ~(SECTOR_SIZE - 1);
            continue;
        }

        if (rec->name_len > 0) {
            char cleaned[64];
            clean_filename(rec->name, rec->name_len, cleaned);

            if (cleaned[0] == 0) {
                offset += rec->length;
                continue;
            }

            if (cleaned[0] == '\1') {
                offset += rec->length;
                continue;
            }

            printf("Entry: %s\n", cleaned);
        }

        offset += rec->length;
    }
}

void iso9660_list_path(const char *path)
{
    iso_dir_record_t *rec;

    if (!path || path[0] == '\0') {
        iso9660_list_root();
        return;
    }

    rec = iso9660_find_path(path);
    if (!rec || !(rec->flags & ISO_FLAG_DIRECTORY)) {
        printf("ls: not a directory: %s\n", path);
        return;
    }

    uint8_t *dir = (uint8_t *)(ISO_BASE + rec->extent_lba_le * SECTOR_SIZE);
    uint32_t size = rec->data_length_le;
    uint32_t offset = 0;

    while (offset < size) {
        iso_dir_record_t *e =
            (iso_dir_record_t *)(dir + offset);

        if (e->length == 0) {
            offset = (offset + SECTOR_SIZE) & ~(SECTOR_SIZE - 1);
            continue;
        }

        char name[ISO_MAX_NAME];
        clean_filename(e->name, e->name_len, name);

        if (name[0])
            printf("Entry: %s\n", name);

        offset += e->length;
    }
}


static int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}


static iso_dir_record_t* iso9660_find_entry(const char *filename)
{
    uint8_t *pvd = (uint8_t *)(ISO_BASE + PVD_SECTOR * SECTOR_SIZE);
    iso_dir_record_t *root = (iso_dir_record_t *)&pvd[156];

    uint32_t root_lba  = root->extent_lba_le;
    uint32_t root_size = root->data_length_le;

    uint8_t *dir = (uint8_t *)(ISO_BASE + root_lba * SECTOR_SIZE);

    uint32_t offset = 0;

    while (offset < root_size) {

        iso_dir_record_t *rec = (iso_dir_record_t *)(dir + offset);

        if (rec->length == 0) {
            offset = (offset + SECTOR_SIZE) & ~(SECTOR_SIZE - 1);
            continue;
        }

        if (ISO_IS_DOT_OR_EMPTY(rec)) {
            offset += rec->length;
            continue;
        }

        char cleaned[64];
        clean_filename(rec->name, rec->name_len, cleaned);

        if (cleaned[0] != 0) {
            if (strcmp(cleaned, filename) == 0) {
                return rec;
            }
        }

        offset += rec->length;
    }

    return NULL;  // not found
}

void iso9660_read_file(const char *path)
{
    iso_dir_record_t *rec = iso9660_find_path(path);

    if (!rec) {
        printf("[DBG] iso9660_find_path('%s') -> NULL\n", path);
        printf("File not found: %s\n", path);
        return;
    }

    printf("[DBG] rec=%p flags=0x%x lba=%u size=%u\n",
       rec,
       rec->flags,
       rec->extent_lba_le,
       rec->data_length_le);

    if (rec->flags & ISO_FLAG_DIRECTORY) {
        printf("Cannot cat directory: %s\n", path);
        return;
    }

    uint32_t lba  = rec->extent_lba_le;
    uint32_t size = rec->data_length_le;

    printf("Reading %s (LBA=%u size=%u)\n", path, lba, size);

    uint8_t *data = (uint8_t *)(ISO_BASE + lba * SECTOR_SIZE);

    // printf("[DBG] data ptr=%p\n", data);
    // printf("[DBG] first 16 bytes:\n");

    // for (int i = 0; i < 16; i++) {
    //     printf("%02x ", data[i]);
    // }
    // printf("\n");


    printf("---- FILE CONTENTS START ----\n");
    for (uint32_t i = 0; i < size; i++)
        printf("%c", data[i]);
    printf("\n---- FILE CONTENTS END ----\n");
}


static int split_path(const char *path,
                      char tokens[ISO_MAX_DEPTH][ISO_MAX_NAME])
{
    int depth = 0, j = 0;

    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') {
            if (j > 0) {
                tokens[depth][j] = '\0';
                depth++;
                j = 0;
            }
            continue;
        }

        if (j < ISO_MAX_NAME - 1) {
            char c = path[i];
            if (c >= 'A' && c <= 'Z')
                c = c + ('a' - 'A');
            tokens[depth][j++] = c;
        }
    }

    if (j > 0) {
        tokens[depth][j] = '\0';
        depth++;
    }

    return depth;
}

static iso_dir_record_t *
find_entry_in_dir(uint32_t dir_lba, uint32_t dir_size, const char *name)
{
    uint8_t *dir = (uint8_t *)(ISO_BASE + dir_lba * SECTOR_SIZE);
    uint32_t offset = 0;

    while (offset < dir_size) {
        iso_dir_record_t *rec = (iso_dir_record_t *)(dir + offset);

        if (rec->length == 0) {
            offset = (offset + SECTOR_SIZE) & ~(SECTOR_SIZE - 1);
            continue;
        }

        printf("[DBG] rec @ offset=%u len=%u name_len=%u flags=0x%x lba=%u size=%u\n",
               offset, rec->length, rec->name_len, rec->flags,
               rec->extent_lba_le, rec->data_length_le);

        /* Skip '.', '..', and empty name records */
        if (ISO_IS_DOT_OR_EMPTY(rec)) {
            offset += rec->length;
            continue;
        }

        char cleaned[ISO_MAX_NAME];
        clean_filename(rec->name, rec->name_len, cleaned);
        printf("[DBG] cleaned name='%s'\n", cleaned);

        if (cleaned[0] && strcmp(cleaned, name) == 0) {
            return rec;
        }

        offset += rec->length;   /* ✅ THIS LINE WAS MISSING */
    }

    return NULL;
}

static iso_dir_record_t *iso9660_find_path(const char *path)
{
    uint8_t *pvd = (uint8_t *)(ISO_BASE + PVD_SECTOR * SECTOR_SIZE);
    iso_dir_record_t *root = (iso_dir_record_t *)&pvd[156];

    uint32_t curr_lba  = root->extent_lba_le;
    uint32_t curr_size = root->data_length_le;

    char tokens[ISO_MAX_DEPTH][ISO_MAX_NAME];
    int depth = split_path(path, tokens);

    iso_dir_record_t *rec = NULL;

    printf("[DBG] iso9660_find_path('%s') depth=%d\n", path, depth);
    printf("[DBG] start at root: lba=%u size=%u\n", curr_lba, curr_size);


    for (int i = 0; i < depth; i++) {
        printf("[DBG] token[%d]='%s'\n", i, tokens[i]);

        rec = find_entry_in_dir(curr_lba, curr_size, tokens[i]);
        if (rec) {
            printf("[DBG] matched '%s': flags=0x%x lba=%u size=%u\n",
                tokens[i], rec->flags, rec->extent_lba_le, rec->data_length_le);
        }

        if (!rec)
            return NULL;

        if (i < depth - 1) {
            if (!(rec->flags & ISO_FLAG_DIRECTORY))
                return NULL;

            curr_lba  = rec->extent_lba_le;
            curr_size = rec->data_length_le;
        }
    }

    return rec;
}

