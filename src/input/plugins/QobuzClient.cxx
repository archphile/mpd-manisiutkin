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

#include "config.h"
#include "QobuzClient.hxx"
#include "lib/gcrypt/MD5.hxx"
#include "util/ConstBuffer.hxx"

#include <stdexcept>

#include <assert.h>

namespace {

class QueryStringBuilder {
	bool first = true;

public:
	QueryStringBuilder &operator()(std::string &dest, const char *name,
				       const char *value) noexcept {
		dest.push_back(first ? '?' : '&');
		first = false;

		dest += name;
		dest.push_back('=');
		dest += value; // TODO: escape

		return *this;
	}
};

}

QobuzClient::QobuzClient(EventLoop &event_loop,
			 const char *_base_url,
			 const char *_app_id, const char *_app_secret,
			 const char *_device_manufacturer_id,
			 const char *_username, const char *_email,
			 const char *_password)
	:base_url(_base_url), app_id(_app_id), app_secret(_app_secret),
	 device_manufacturer_id(_device_manufacturer_id),
	 username(_username), email(_email), password(_password),
	 curl(event_loop),
	 defer_invoke_handlers(event_loop, BIND_THIS_METHOD(InvokeHandlers))
{
}

CurlGlobal &
QobuzClient::GetCurl() noexcept
{
	return *curl;
}

void
QobuzClient::StartLogin() noexcept
{
	assert(!session.IsDefined());
	assert(!login_request);
	assert(!handlers.empty());

	QobuzLoginHandler &handler = *this;
	login_request = std::make_unique<QobuzLoginRequest>(*curl, base_url,
							    app_id,
							    username, email,
							    password,
							    device_manufacturer_id,
							    handler);
	login_request->Start();
}

void
QobuzClient::AddLoginHandler(QobuzSessionHandler &h) noexcept
{
	const std::lock_guard<Mutex> protect(mutex);
	assert(!h.is_linked());

	const bool was_empty = handlers.empty();
	handlers.push_front(h);

	if (!was_empty || login_request)
		return;

	if (session.IsDefined()) {
		ScheduleInvokeHandlers();
	} else {
		// TODO: throttle login attempts?

		std::string login_uri(base_url);
		login_uri += "/login/username";

		try {
			StartLogin();
		} catch (...) {
			error = std::current_exception();
			ScheduleInvokeHandlers();
			return;
		}
	}
}

QobuzSession
QobuzClient::GetSession() const
{
	const std::lock_guard<Mutex> protect(mutex);

	if (error)
		std::rethrow_exception(error);

	if (!session.IsDefined())
		throw std::runtime_error("No session");

	return session;
}

void
QobuzClient::OnQobuzLoginSuccess(QobuzSession &&_session) noexcept
{
	{
		const std::lock_guard<Mutex> protect(mutex);
		session = std::move(_session);
	}

	ScheduleInvokeHandlers();
}

void
QobuzClient::OnQobuzLoginError(std::exception_ptr _error) noexcept
{
	{
		const std::lock_guard<Mutex> protect(mutex);
		error = std::move(_error);
	}

	ScheduleInvokeHandlers();
}

void
QobuzClient::InvokeHandlers() noexcept
{
	const std::lock_guard<Mutex> protect(mutex);
	while (!handlers.empty()) {
		auto &h = handlers.front();
		handlers.pop_front();

		const ScopeUnlock unlock(mutex);
		h.OnQobuzSession();
	}

	login_request.reset();
}

std::string
QobuzClient::MakeSignedUrl(const char *object, const char *method,
			   const std::multimap<std::string, std::string> &query) const noexcept
{
	assert(!query.empty());

	std::string uri(base_url);
	uri += object;
	uri.push_back('/');
	uri += method;

	QueryStringBuilder q;
	std::string concatenated_query(object);
	concatenated_query += method;
	for (const auto &i : query) {
		q(uri, i.first.c_str(), i.second.c_str());

		concatenated_query += i.first;
		concatenated_query += i.second;
	}

	q(uri, "app_id", app_id);

	const auto request_ts = std::to_string(time(nullptr));
	q(uri, "request_ts", request_ts.c_str());
	concatenated_query += request_ts;

	concatenated_query += app_secret;

	const auto md5_hex = MD5Hex({concatenated_query.data(), concatenated_query.size()});
	q(uri, "request_sig", &md5_hex.front());

	return uri;
}
