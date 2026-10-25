/* SPDX-License-Identifier: GPL-2.0-or-later
 * speech-dispatcher 0.12 installs spd_module_main.h referring to this private
 * header, but only installs spd_audio_plugin.h. The API needs its public types.
 */
#include <spd_audio_plugin.h>
