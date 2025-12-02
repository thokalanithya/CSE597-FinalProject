#include <types.h>
#include <printf.h>
#include "iso9660.h"

#define SECTOR_SIZE 2048
#define PVD_SECTOR 16

static uint32_t ISO_BASE = 0;
static uint32_t ISO_SIZE = 0;

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

void iso9660_init(uint32_t base, uint32_t size)
{
    ISO_BASE = base;
    ISO_SIZE = size;

    printf("ISO9660: initialized at %x (size %u)\n", ISO_BASE, ISO_SIZE);
}

static void clean_filename(char *name, int len, char *out)
{
    int i;
    for (i = 0; i < len; i++) {
        if (name[i] == ';') {
            break;
        }
        out[i] = name[i];
    }
    out[i] = '\0';
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

void iso9660_read_file(const char *filename)
{
    iso_dir_record_t *rec = iso9660_find_entry(filename);

    if (!rec) {
        printf("File not found: %s\n", filename);
        return;
    }

    uint32_t lba = rec->extent_lba_le;
    uint32_t size = rec->data_length_le;

    printf("Reading %s (LBA=%u size=%u)\n", filename, lba, size);

    uint8_t *data = (uint8_t *)(ISO_BASE + lba * SECTOR_SIZE);

    printf("---- FILE CONTENTS START ----\n");

    for (uint32_t i = 0; i < size; i++) {
        printf("%c", data[i]);
    }

    printf("\n---- FILE CONTENTS END ----\n");
}

