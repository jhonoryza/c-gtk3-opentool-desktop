# Config Opener - Sublime Text Launcher

Aplikasi desktop ringan berbasis **C murni** dan **GTK3** untuk macOS yang menampilkan daftar file konfigurasi dan membukanya dengan Sublime Text.

![Platform](https://img.shields.io/badge/platform-macOS%2013%20Ventura-blue)
![Language](https://img.shields.io/badge/language-C99-orange)
![GTK](https://img.shields.io/badge/GTK-3-green)
![License](https://img.shields.io/badge/license-MIT-green)

## 📋 Daftar Isi

- [Fitur](#fitur)
- [Persyaratan](#persyaratan)
- [Instalasi](#instalasi)
  - [1. Install GTK3 + pkg-config](#1-install-gtk3--pkg-config)
  - [2. Install Sublime Text CLI](#2-install-sublime-text-cli)
  - [3. Clone & Compile](#3-clone--compile)
- [Penggunaan](#penggunaan)
- [Struktur Proyek](#struktur-proyek)
- [Teknis](#teknis)
- [License](#license)

## ✨ Fitur

- Menampilkan 7 file konfigurasi umum dalam grid 2 kolom
- Tombol **"Open with Sublime Text"** untuk setiap file
- Scrolled window untuk daftar yang panjang
- Validasi: error jika `subl` tidak ditemukan, warning jika file belum ada
- Path dengan spasi di-handle dengan benar (`fork()` + `execlp()`)
- Ekspansi `~` ke home directory
- Path dinamis untuk user-specific config menggunakan `$USER`

### Daftar File Konfigurasi

| Nama Config | Path |
|-------------|------|
| opencode | `~/.config/opencode/opencode.jsonc` |
| gowails chatai | `/Users/$USER/Library/Application Support/gowails-chatai-desktop/settings.json` |
| zshrc pre | `~/.zshrc.pre-oh-my-zsh` |
| zshrc | `~/.zshrc` |
| tmux | `~/.tmux.conf` |
| ghossty | `~/.config/ghostty/config` |
| zed | `~/.config/zed/settings.json` |

## 📦 Persyaratan

- **macOS 13 Ventura** atau lebih baru
- **GCC** (Apple Clang bawaan Xcode sudah cukup)
- **GTK3** (`gtk±3.0`)
- **pkg-config**
- **Sublime Text** dengan CLI `subl` di PATH

## 🔧 Instalasi

### 1. Install GTK3 + pkg-config

```bash
brew install gtk+3 pkg-config
```

Verifikasi instalasi:

```bash
pkg-config --cflags --libs gtk+-3.0
```

### 2. Install Sublime Text CLI

Pastikan Sublime Text sudah terinstall, lalu buat symlink:

```bash
ln -s /Applications/Sublime\ Text.app/Contents/SharedSupport/bin/subl /usr/local/bin/subl
```

Verifikasi:

```bash
subl --version
```

### 3. Clone & Compile

```bash
# Clone repositori
git clone https://github.com/username/config-opener.git
cd config-opener

# Compile dengan Make
make

# Atau compile manual
gcc -std=c99 -Wall -Wextra -o config-opener main.c $(pkg-config --cflags --libs gtk+-3.0)
```

### 4. Install sebagai macOS App (Optional)

Untuk bisa membuka dari Spotlight (Cmd+Space):

```bash
# Install ke /Applications
make install-app

# Uninstall
make uninstall-app
```

## 🚀 Penggunaan

### CLI Mode
```bash
# Jalankan aplikasi
make run

# Atau langsung
./config-opener
```

### macOS App (Spotlight)
```bash
# Install sebagai macOS app (bisa dibuka dari Spotlight)
make install-app

# Atau gunakan script installer
./install.sh

# Uninstall
make uninstall-app
# atau
./uninstall.sh
```

Setelah install, buka Spotlight (Cmd+Space) dan cari "Config Opener".

## 📁 Struktur Proyek

```
config-opener/
├── main.c           # Kode utama aplikasi
├── Makefile         # Build system
├── Info.plist       # macOS app bundle metadata
├── install.sh       # Installer script
├── uninstall.sh     # Uninstaller script
├── README.md        # Dokumentasi (file ini)
└── LICENSE          # Lisensi MIT
```

## ⚙️ Teknis

| Aspek | Detail |
|-------|--------|
| Bahasa | C99 / C11 murni (tanpa C++) |
| GUI Toolkit | GTK 3.24+ (bukan GTK4) |
| UI Builder | Programmatic (tanpa Glade/XML) |
| Proses | `fork()` + `execlp()` |
| Window | 500×300, title "Config Opener - Sublime Text Launcher" |
| Widget | `GtkWindow` → `GtkBox` → `GtkScrolledWindow` → `GtkGrid` |
| Error Handling | Dialog `gtk_message_dialog_new` untuk subl not found & fork failure |
| File Warning | Dialog warning jika file tidak ada (tetap boleh open) |
| Zombie Reaping | `signal(SIGCHLD, SIG_IGN)` |

### Path dengan Spasi

`execlp()` mengirim argumen sebagai elemen `argv` terpisah — tidak melalui shell. Path seperti `/Users/user/Library/Application Support/...` yang mengandung spasi akan tetap dianggap sebagai satu argumen oleh Sublime Text, tanpa perlu quoting manual.

### Ekspansi Path

- `~` diekspansi menggunakan `g_get_home_dir()` (membaca `$HOME` atau fallback ke `/etc/passwd`)
- Path user-specific menggunakan `g_get_user_name()` (membaca `$USER` atau fallback ke `/etc/passwd`)

## 📄 License

Proyek ini dilisensikan di bawah **MIT License** — lihat file [LICENSE](LICENSE) untuk detail.
