# CC and LD variables (change for newer Macs)
CC=x86_64-linux-gnu-gcc
LD=x86_64-linux-gnu-ld

OVMF=/usr/share/ovmf/OVMF.fd

BOOT=boot.img
KERNEL=kernel

all: $(BOOT)

run: $(BOOT)
	@qemu-system-x86_64 -m 512 --bios $(OVMF) -drive format=raw,file=$(BOOT)

$(BOOT): $(KERNEL)
	@if [ -d ./uefi_fat_mnt ]; then sudo umount -q ./uefi_fat_mnt || true; fi
	@if [ -d ./uefi_fat_mnt ]; then rmdir ./uefi_fat_mnt; fi
	@mkdir ./uefi_fat_mnt
	@dd if=/dev/zero of=$(BOOT) bs=1M count=10
	@mkfs.vfat ./$(BOOT)
	@sudo mount -o loop ./$(BOOT) ./uefi_fat_mnt
	@sudo mkdir -p ./uefi_fat_mnt/EFI/BOOT
	@sudo mkdir -p ./uefi_fat_mnt/EFI/ubuntu/x86_64-efi
	@sudo cp ./grub/grub.cfg ./uefi_fat_mnt/EFI/BOOT/grub.cfg
	@sudo cp ./grub/grubx64.efi ./uefi_fat_mnt/EFI/BOOT/BOOTX64.EFI
	@sudo cp ./grub/*.mod ./uefi_fat_mnt/EFI/ubuntu/x86_64-efi/
	@sudo cp ./$(KERNEL) ./uefi_fat_mnt/kernel
	@sudo umount ./uefi_fat_mnt
	@rmdir ./uefi_fat_mnt

# Kernel and user program compilation
CFLAGS += -mcmodel=small -Wall -Wno-builtin-declaration-mismatch -O2 -fno-pie -mno-red-zone -nostdinc -fno-stack-protector -fno-zero-initialized-in-bss -fno-builtin -c
LDFLAGS = -nostdlib -melf_x86_64

KERNEL_OBJS = kernel_entry.o # Do not reorder
KERNEL_OBJS += kernel.o kernel_asm.o apic.o ascii_font.o fb.o printf.o

$(KERNEL): $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -T ./kernel.lds $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -I ./include -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -I ./include -c -o $@ $<

clean:
	@rm -rf $(KERNEL) $(KERNEL_OBJS) $(BOOT)
