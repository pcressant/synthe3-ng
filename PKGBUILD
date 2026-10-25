# SPDX-License-Identifier: GPL-2.0-or-later
# Recette locale : extraire/copier les sources à côté de ce PKGBUILD, puis makepkg -si.
# Pour l'AUR, remplacer la source locale par l'archive d'une version publiée (voir docs/packaging.md).
pkgname=synthe3-ng
pkgver=0.3.0
pkgrel=1
pkgdesc='Moteur vocal français Michel en C pour Speech Dispatcher'
url='https://github.com/pcressant/synthe3-ng'
arch=('x86_64' 'aarch64')
license=('GPL-2.0-or-later' 'LGPL-2.1-or-later')
depends=('speech-dispatcher>=0.12' 'libxml2' 'glibc')
makedepends=('gcc' 'make' 'pkgconf')
checkdepends=('python' 'strace')
source=('Makefile' 'src' 'tests' 'config' 'upstream' 'docs' 'README.md' 'UPSTREAM.md' 'LICENSE.md')
sha256sums=('SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP')

build() {
  make
}

check() {
  make test
}

package() {
  make DESTDIR="$pkgdir" PREFIX=/usr SYSCONFDIR=/etc install
}
