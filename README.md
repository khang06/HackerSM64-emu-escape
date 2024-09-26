# Parallel Launcher 2.14.3 Emulator Escape PoC

This is some old proof-of-concept code for exploiting an OOB .data array read/write primitive in Parallel Launcher's mupen64plus fork using SRAM DMA.

It's able to recover from exploitation and install a hook to run arbitrary native code using an undefined opcode in the guest.

This bug was fixed in commit [ff3104c874af7a074902d7a569ceaafe8038f739](https://gitlab.com/parallel-launcher/parallel-n64/-/commit/ff3104c874af7a074902d7a569ceaafe8038f739).

https://github.com/user-attachments/assets/38db5747-5e36-4108-9ab2-a0eedeb77a6a

