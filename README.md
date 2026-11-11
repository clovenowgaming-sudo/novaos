# Nova OS 0.1

From-scratch x86_64 operating system in C and assembly. Boots via GRUB (BIOS and UEFI ISO).

## Works now
- Installer wizard, lock screen, sign-in, Start menu, taskbar, windows (drag, minimize, maximize, close)
- Terminal, Notepad, File Explorer, Browser (local HTML pages)
- In-memory file system, PS/2 keyboard and mouse, RTC clock

- **Install ke disk** (ATA/IDE): installer menulis GRUB (BIOS + UEFI) + sistem ke disk, file & akun tersimpan permanen (partisi data Nova, slot A/B)
- **Menjalankan .exe** PE32 (x86) dan PE32+ (x64) lewat loader sendiri + lapisan Win32/CRT minimal (lihat batasan)

## Not yet (see nova://roadmap in the browser)
- USB keyboard/mouse (needed on most real PCs), Ethernet, TCP/IP, TLS/https, AHCI/NVMe, WiFi, Bluetooth

## Build
    sudo apt install xorriso grub-pc-bin grub-efi-amd64-bin mtools build-essential
    make            # produces nova-os.iso
    make run        # ISO + disk kosong disk.img (512MB) di QEMU -> jalankan installer, pilih "Disk 0"
    make run-disk   # boot dari disk.img (setelah terpasang)
    make run-disk-uefi   # sama, tapi UEFI (butuh OVMF)

## Publish + build on GitHub
    ./publish.sh nova-os    # gh repo create + git push; Actions builds and uploads the ISO to Releases

## Changelog
- Fix: pointer mouse patah-patah dan "teleport" saat drag. Input mouse/keyboard sekarang pakai interrupt (IDT + PIC, IRQ1/IRQ12) sehingga tidak ada byte PS/2 yang hilang saat render lambat; paket overflow/nyasar dibuang dan disinkronkan ulang; pointer digambar langsung ke framebuffer terpisah dari scene; rendering rounded-rect dan bayangan jendela dioptimasi.

## Install ke disk
1. `make run` -> di installer pilih *Disk 0*, centang konfirmasi hapus, buat akun, tunggu 100%.
2. Restart, lalu `make run-disk` (boot dari disk saja). File/akun tetap ada setelah restart.
- Disk harus >= 32 MB, mode IDE/ATA. Di PC asli set SATA ke *IDE/Compatibility* (AHCI/NVMe belum didukung).
- Syarat RAM: >= 256 MB (kernel di 64MB, area 4..60MB dipakai program .exe).
- Menambah file (mis. .exe) ke disk dari host: `python3 tools/novafs.py disk.img add prog.exe /Programs/prog.exe`

## Menjalankan .exe
- Taruh `.exe` di folder `apps/` sebelum `make` (otomatis masuk `/Programs` saat live boot & ikut terinstal), atau pakai `novafs.py`.
- Di Terminal: `hello64` / `run hello64.exe`. Dobel-klik .exe di File Explorer juga jalan di jendela Terminal.
- Contoh: `apps/hello32.exe` (x86) & `apps/hello64.exe` (x64), dibuat `python3 tools/mkpe.py`.
- **Batasan jujur:** ini lapisan kompatibilitas minimal, bukan Windows. Program console sederhana (kernel32 + msvcrt/ucrt dasar:
  printf, malloc, string, getchar, MessageBox) bisa jalan. Aplikasi GUI (GDI/DirectX), DLL pihak ketiga, thread, file I/O Win32, float di printf
  selain %f/%g dasar belum ada. Import yang belum ada -> program berhenti dengan pesan `Unimplemented import: <nama>`.
  Program jalan di ring 0 tanpa proteksi memori (bug program bisa crash OS; exception ditangkap & ditampilkan bila memungkinkan).

## Changelog
- Install nyata ke disk (ATA PIO) + penyimpanan permanen; loader PE32/PE32+; kernel dipindah ke 64MB; handler exception CPU; build tanpa nasm (boot.S).
