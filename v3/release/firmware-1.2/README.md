# V3 firmware 1.2 and ETS product (application 0x4741 version 3)

Built 2026-11-10 from [`../../firmware/`](../../firmware/).

| File | Flash at | Use |
| --- | --- | --- |
| `garantia-knx-u3-app.hex` / `.bin` | 0x0800 1000 | **Every update.** Application 1.2, 60 648 bytes, CRC-32 `85D35C8D`. It never touches the boot manager, so an interrupted update stays recoverable |
| `garantia-knx-u3-full.hex` / `.bin` | 0x0800 0000 | **A blank chip only.** Boot manager v2 + application 1.2. A power cut while it writes page 0 needs an SWD probe to recover |
| `garantia-knx.knxprod` | — | ETS6 product file, application version 3. |

How to flash, enter DFU and commission is in [`../../firmware/README.md`](../../firmware/README.md).

These images link [thelsing/knx](https://github.com/thelsing/knx) (GPL-3.0), so they are distributed under GPL-3.0. Their source is [`../../firmware/`](../../firmware/), plus thelsing/knx `980c047` with `patches/knx-thelsing.patch`.
