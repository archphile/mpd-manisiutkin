/*
 * Copyright 2003-2018 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef QOBUZ_TRACK_REQUEST_HXX
#define QOBUZ_TRACK_REQUEST_HXX

#include "check.h"
#include "lib/curl/Handler.hxx"
#include "lib/curl/Slist.hxx"
#include "lib/curl/Request.hxx"
#include "lib/yajl/Handle.hxx"

#include <exception>
#include <string>

class QobuzClient;
struct QobuzSession;

class QobuzTrackHandler
	: public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::safe_link>>
{
public:
	virtual void OnQobuzTrackSuccess(std::string &&url) noexcept = 0;
	virtual void OnQobuzTrackError(std::exception_ptr error) noexcept = 0;
};

class QobuzTrackRequest final : CurlResponseHandler {
	CurlSlist request_headers;

	CurlRequest request;

	Yajl::Handle parser;

	enum class State {
		NONE,
		URL,
	} state = State::NONE;

	std::string url;

	std::exception_ptr error;

	QobuzTrackHandler &handler;

public:
	QobuzTrackRequest(QobuzClient &client, const QobuzSession &session,
			  const char *track_id,
			  QobuzTrackHandler &_handler) noexcept;

	~QobuzTrackRequest() noexcept;

	void Start() noexcept {
		request.StartIndirect();
	}

private:
	/* virtual methods from CurlResponseHandler */
	void OnHeaders(unsigned status,
		       std::multimap<std::string, std::string> &&headers) override;
	void OnData(ConstBuffer<void> data) override;
	void OnEnd() override;
	void OnError(std::exception_ptr e) noexcept override;

public:
	/* yajl callbacks */
	bool String(StringView value) noexcept;
	bool MapKey(StringView value) noexcept;
	bool EndMap() noexcept;
};

#endif
