/*
* MPD SACD Decoder plugin
* Copyright (c) 2014-2019 Maxim V.Anisiutkin <maxim.anisiutkin@gmail.com>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with FFmpeg; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifndef _DST_DECODER_H_INCLUDED
#define _DST_DECODER_H_INCLUDED

#include <thread>
#include <vector>
#include <semaphore.h>
#include "DSTDecoder.h"

using std::thread;
using std::vector;

enum slot_state_t {SLOT_EMPTY, SLOT_LOADED, SLOT_RUNNING, SLOT_READY, SLOT_READY_WITH_ERROR, SLOT_TERMINATING};

class frame_slot_t {
public:
	bool         run_slot;
	thread       run_thread;
	semaphore    dsd_semaphore;
	semaphore    dst_semaphore;

	slot_state_t state;
	int          frame_nr;
	uint8_t*     dsd_data;
	int          dsd_size;
	uint8_t*     dst_data;
	int          inp_size;
	int          channel_count;
	int          samplerate;
	int          framerate;
	CDSTDecoder  D;

	frame_slot_t() {
		run_slot = false;
		state = SLOT_EMPTY;
		frame_nr = -1;
		dsd_data = nullptr;
		dsd_size = 0;
		dst_data = nullptr;
		inp_size = 0;
		channel_count = 0;
		samplerate = 0;
		framerate = 0;
	}
	frame_slot_t(const frame_slot_t& slot) {
	}
};

class dst_decoder_t {
	vector<frame_slot_t> frame_slots;
	int slot_nr;
	int frame_nr;
	int channel_count;
	int samplerate;
	int framerate;
public:
	dst_decoder_t(int threads);
	~dst_decoder_t();
	int get_slot_nr();
	int init(int channel_count, int samplerate, int framerate);
	int decode(uint8_t* dst_data, size_t dst_size, uint8_t** dsd_data, size_t* dsd_size);
};

#endif
