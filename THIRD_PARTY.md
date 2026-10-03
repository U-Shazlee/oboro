# Third-party software

Oboro is licensed under the GNU General Public License v3.0 (see
[LICENSE](LICENSE)). It is derived from, includes or links the following
software, each under its own licence. All of these licences are compatible
with GPL-3.0.

## Derived from

| Project | Licence | What comes from it |
|---|---|---|
| [Kasumi](https://github.com/p0mpurin/Kasumi), Copyright p0mpurin | GPL-3.0 | `source/`, `include/`, `gfx/`, `resources/`, `romfs/` and the Makefile, modified: the GeForce NOW client, NVST signalling and WebRTC transport were removed and replaced by `host_client.c`, `moon_transport.c` and `pc_stats.c` |
| [Moonlight-N3DS](https://github.com/zoeyjodon/moonlight-N3DS), Copyright zoeyjodon; from [Moonlight Embedded](https://github.com/moonlight-stream/moonlight-embedded), Copyright Iwan Timmer | GPL-3.0 | `libgamestream/` (modified: HTTP symbols renamed to `gs_http_*`, request cancelling, `gs_app_asset`), `tools/build-deps.sh` |

## Included in this repository

| Project | Licence | Use |
|---|---|---|
| [stb_image, stb_image_write](https://github.com/nothings/stb) (`vendor/stb/`) | MIT or public domain (in each header) | Box art decoding, PNG screenshots |
| libuuid from util-linux (`third_party/libuuid/`) | BSD-3-Clause (`third_party/libuuid/COPYING`) | Request IDs for the host |

## Fetched at build time (`tools/fetch-deps.sh`)

| Project | Licence | Use |
|---|---|---|
| [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c) (with its ENet fork and reedsolomon) | GPL-3.0 (ENet: MIT; reedsolomon: BSD-style) | The Moonlight streaming protocol |
| [OpenSSL](https://github.com/zoeyjodon/openssl) (zoeyjodon's 3DS target) | Apache-2.0 | Pairing: certificate, signatures, AES |
| [libexpat](https://github.com/libexpat/libexpat) | MIT | Parsing the host's XML answers |

## Linked from devkitPro portlibs

| Project | Licence |
|---|---|
| [libctru](https://github.com/devkitPro/libctru), [citro3d](https://github.com/devkitPro/citro3d), [citro2d](https://github.com/devkitPro/citro2d) | zlib |
| [libcurl](https://curl.se/) | curl licence (MIT-style) |
| [Jansson](https://github.com/akheron/jansson) | MIT |
| [Opus](https://opus-codec.org/) | BSD-3-Clause |
| [zlib](https://zlib.net/) | zlib |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | Apache-2.0 |

`romfs/cacert.pem` is the Mozilla CA certificate list as distributed by curl
(MPL-2.0).

Nintendo 3DS is a trademark of Nintendo. NVIDIA and GeForce NOW are
trademarks of NVIDIA Corporation. Oboro is not affiliated with or endorsed by
Nintendo, NVIDIA, the Moonlight project, LizardByte or the Kasumi project.
