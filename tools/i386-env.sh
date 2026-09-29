# source me: compile/link flags for the static i386 translator input
TC=${TC:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build/i386tc}
I386_CFLAGS="-m32 -march=i686 -mno-sse -mno-mmx -mno-80387 -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -isystem $TC/musl/include"
I386_LDFLAGS="-m32 -static -nostdlib -no-pie -Wl,--emit-relocs -Wl,-Ttext-segment=0x100000 -Wl,--build-id=none"
I386_CRT_BEGIN="$TC/musl/lib/crt1.o $TC/musl/lib/crti.o"
I386_CRT_END="$TC/musl/lib/libc.a $TC/gcc-lib/libgcc.a $TC/musl/lib/crtn.o"
