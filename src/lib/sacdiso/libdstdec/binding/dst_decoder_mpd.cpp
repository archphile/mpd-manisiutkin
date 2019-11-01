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

#include <malloc.h>
#include <memory.h>
#include <stdio.h>
#include <stdarg.h>
//#include "stddef.h"
#include "dst_decoder_mpd.h"

using namespace std;

#define DSD_SILENCE_BYTE 0x69

#define LOG_ERROR ("ERROR: ")
#define LOG(p1, p2)

void dst_run_thread(frame_slot_t* slot) {
	while (slot->run_slot) {
		slot->dst_semaphore.wait();
		if (slot->run_slot) {
			slot->state = SLOT_RUNNING;
			slot->D.decode(slot->dst_data, slot->inp_size * 8, slot->dsd_data);
			slot->state = SLOT_READY;
		}
		else {
			slot->dsd_data = nullptr;
			slot->inp_size = 0;
		}
		slot->dsd_semaphore.notify();
	}
}

dst_decoder_t::dst_decoder_t(int threads) {
	frame_slots.resize(threads);
	slot_nr       = 0;
	channel_count = 0;
	samplerate    = 0;
	framerate     = 0;
}

dst_decoder_t::~dst_decoder_t() {
	for (auto& slot : frame_slots) {
		slot.state = SLOT_TERMINATING;
		slot.D.close();
		slot.run_slot = false;
		slot.dst_semaphore.notify(); // Release worker (decoding) thread for exit
		slot.run_thread.join(); // Wait until worker (decoding) thread exit
	}
}

int dst_decoder_t::get_slot_nr() {
	return slot_nr;
}

int dst_decoder_t::init(int channel_count, int samplerate, int framerate) {
	for (auto& slot : frame_slots)	{
		if (slot.D.init(channel_count, (samplerate / 44100) / (framerate / 75)) == 0) {
			slot.channel_count = channel_count;
			slot.samplerate = samplerate;
			slot.framerate = framerate;
			slot.dsd_size = (size_t)(samplerate / 8 / framerate * channel_count);
			slot.run_slot = true;
			slot.run_thread = thread(dst_run_thread, &slot);
			if (!slot.run_thread.joinable()) {
				LOG(LOG_ERROR, ("Could not start decoder thread"));
				return -1;
			}
		}
		else {
			LOG(LOG_ERROR, ("Could not initialize decoder slot"));
			return -1;
		}
	}
	this->channel_count = channel_count;
	this->samplerate = samplerate;
	this->framerate = framerate;
	this->frame_nr = 0;
	return 0;
}

int dst_decoder_t::decode(uint8_t* dst_data, size_t dst_size, uint8_t** dsd_data, size_t* dsd_size) {

	/* Get current slot */
	frame_slot_t& slot_set = frame_slots[slot_nr];

	/* Allocate encoded frame into the slot */
	slot_set.dsd_data = *dsd_data;
	slot_set.dst_data = dst_data;
	slot_set.inp_size = dst_size;
	slot_set.frame_nr = frame_nr;

	/* Release worker (decoding) thread on the loaded slot */
	if (dst_size > 0)	{
		slot_set.state = SLOT_LOADED;
		slot_set.dst_semaphore.notify();
	}
	else {
		slot_set.state = SLOT_EMPTY;
	}

	/* Move to the oldest slot */
	slot_nr = (slot_nr + 1) % frame_slots.size();
	frame_slot_t& slot_get = frame_slots[slot_nr];

	/* Dump decoded frame */
	if (slot_get.state != SLOT_EMPTY) {
		slot_get.dsd_semaphore.wait();
	}
	switch (slot_get.state) {
	case SLOT_READY:
		*dsd_data = slot_get.dsd_data;
		*dsd_size = (size_t)(samplerate / 8 / framerate * channel_count);
		break;
	case SLOT_READY_WITH_ERROR:
		*dsd_data = slot_get.dsd_data;
		*dsd_size = (size_t)(samplerate / 8 / framerate * channel_count);
		memset(*dsd_data, DSD_SILENCE_BYTE, *dsd_size);
		break;
	default:
		*dsd_data = nullptr;
		*dsd_size = 0;
		break;
	}
	frame_nr++;
	return 0;
}


