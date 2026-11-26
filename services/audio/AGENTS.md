# services/audio/

Audio services: the microphone as a device, and the Opus encoder.

## The microphone (`tiku_mic.c`, `tiku_mic_vfs.c`)

`TIKU_MIC_ENABLE=1` builds a kernel process that drains the port's
capture into a PSRAM ring of `TIKU_MIC_RING_SAMPLES` mono 16 kHz PCM16
samples (four seconds, 128 KB), and two boot providers:

| Node | Behavior |
|---|---|
| `/dev/mic` | Character device.  Each POSIX read moves the oldest waiting samples out as little-endian PCM16, as many whole samples as the buffer holds.  It returns 0 bytes when capture runs and none wait, `ENOTSUP` while the microphone is off, and `EIO` once a failed capture's ring is empty.  A text read (the shell's `read`) renders up to 32 of the oldest samples in decimal and moves them out too. |
| `/sys/audio/mic/enable` | `1` starts capture, `0` stops it and frees the ring; requires `TIKU_VFS_CAP_HW`.  A start while running and a stop while idle change nothing. |
| `/sys/audio/mic/gain` | The microphones' gain in whole dB, 0..36, 30 at boot; requires `TIKU_VFS_CAP_HW`.  It applies at once while capture runs and at each start; a value the port refuses is kept and reported as `EIO`. |
| `/sys/audio/mic/state` | `idle`, `running` or `failed`. |
| `/sys/audio/mic/rate` | 16000. |
| `/sys/audio/mic/buffered`, `captured`, `dropped`, `faults` | Samples waiting, samples put in the ring since the start, samples lost to a full ring, and port errors. |
| `/sys/audio/mic/peak` | The largest magnitude in the last block, 0..32767. |

The process wakes every 16 ms or less and moves every ready block into
the ring.  It drops the first `TIKU_MIC_SETTLE_BLOCKS` (32) blocks after a
start, while the codec's input settles.  A full ring keeps its oldest
samples and counts the rest in `dropped`; a reader that falls more than
four seconds behind loses audio.  A port error stops capture, closes the
port and leaves the ring readable until the next stop or start.

Every call, the process and the VFS handlers run on the kernel thread;
a worker reaches them through the POSIX library.  A port supplies
`tiku_mic_arch_open()`, `_poll()`, `_gain()` and `_close()`:
`tiku_mic_s31.c` averages the Korvo's two microphones from I2S0 and the
ES8389 and sets the codec's input PGA (`tiku_drv_audio_es8389_input()`); the
S31 build selects it with `S31_MIC=1` (`arch/esp32s31/README.md`).

## Opus (`tiku_opus.c`)

`tiku_opus.h` is a mono 16 kHz, 24 kbit/s Ogg Opus encoder over
caller-owned state; `services/storage/tiku_media_record_opus.c` uses it
for the P4's recordings (`kernel/fs/media.md`).

## Proof

Host: `TikuBench/tests/host/kernel/test_mic.c` drives the capture process
over a scripted port, built with a 1300-sample ring so blocks straddle
its end; `mutations/kernel.py` model `mic`.  `test_vfs_assembly.c`
mounts a node under `/dev` and boots the `mic` profile.
