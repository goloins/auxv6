# dd(1)

## Name
dd - Copy and convert data with block-oriented control.

## Synopsis
```sh
dd [if=file] [of=file] [bs=n] [ibs=n] [obs=n] [count=n] [skip=n] [seek=n]
   [conv=notrunc,sync,noerror] [iflag=fullblock] [oflag=append]
   [status=none|noxfer]
```

## Duty
Read data from `if` (or stdin), write to `of` (or stdout), and control transfer
size/offset behavior for regular files and block devices.

## Arguments
- `if=file` - input path. Defaults to stdin.
- `of=file` - output path. Defaults to stdout.
- `bs=n` - set both input and output block size.
- `ibs=n` - input block size (default 512).
- `obs=n` - output block size (default 512).
- `count=n` - copy at most `n` input blocks.
- `skip=n` - skip `n` input blocks before copying.
- `seek=n` - skip `n` output blocks before writing.

## Conversions and Flags
- `conv=notrunc` - do not truncate output file on open.
- `conv=sync` - pad short input blocks with zeros to `ibs` bytes.
- `conv=noerror` - continue after input read errors.
- `iflag=fullblock` - keep reading until a full input block or EOF.
- `oflag=append` - open output in append mode.
- `status=none` - suppress transfer summary output.
- `status=noxfer` - print records summary but hide byte count line.

## Number Suffixes
`n` accepts decimal/hex input and these suffixes:
- `c` (1), `w` (2), `b` (512)
- `k`/`K` (1024), `kB`/`KB` (1000)
- `m`/`M` (1024*1024), `MB` (1000*1000)
- `g`/`G` (1024*1024*1024), `GB` (1000*1000*1000)
- Multipliers using `x`, `X`, or `*` (example: `bs=4x1024`).

## Notes
- `skip` and `seek` are block counts, not byte offsets.
- For seekable input, `skip` uses `lseek`; otherwise it reads/discards blocks.
- Summary output is written to stderr, similar to traditional `dd` behavior.

## Examples
```sh
# Clone a block device image with 4 KiB blocks
# (example device names depend on your /dev layout)
dd if=/dev/hd0 of=/tmp/disk.img bs=4K

# Write image back, preserving existing file length when supported
dd if=/tmp/disk.img of=/dev/hd0 bs=4K conv=notrunc

# Copy first 100 blocks, skipping one block on input
dd if=/dev/hd0 of=/tmp/header.bin ibs=512 obs=512 skip=1 count=100
```

## Source Audit
- Source file: user/dd.c
- Last updated: 2026-04-06
