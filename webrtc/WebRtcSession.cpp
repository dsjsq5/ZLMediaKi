﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xia-chu/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "WebRtcSession.h"
#include "Util/util.h"

using namespace std;

namespace mediakit {

static string getUserName(const Buffer::Ptr &buffer) {
    auto buf = buffer->data() + 2;
    auto len = buffer->size() - 2;
    if (!RTC::StunPacket::IsStun((const uint8_t *) buf, len)) {
        return "";
    }
    std::unique_ptr<RTC::StunPacket> packet(RTC::StunPacket::Parse((const uint8_t *) buf, len));
    if (!packet) {
        return "";
    }
    if (packet->GetClass() != RTC::StunPacket::Class::REQUEST ||
        packet->GetMethod() != RTC::StunPacket::Method::BINDING) {
        return "";
    }
    //收到binding request请求
    auto vec = split(packet->GetUsername(), ":");
    return vec[0];
}

EventPoller::Ptr WebRtcSession::queryPoller(const Buffer::Ptr &buffer) {
    auto user_name = getUserName(buffer);
    if (user_name.empty()) {
        return nullptr;
    }
    auto ret = WebRtcTransportManager::Instance().getItem(user_name);
    return ret ? ret->getPoller() : nullptr;
}

////////////////////////////////////////////////////////////////////////////////

WebRtcSession::WebRtcSession(const Socket::Ptr &sock) : TcpSession(sock) {
    socklen_t addr_len = sizeof(_peer_addr);
    getpeername(sock->rawFD(), (struct sockaddr *)&_peer_addr, &addr_len);
}

WebRtcSession::~WebRtcSession() {
    InfoP(this);
}

void WebRtcSession::onRecv(const Buffer::Ptr &buffer) {
    //只允许寻找一次transport
    if (!_transport) {
        auto user_name = getUserName(buffer);
        auto transport = WebRtcTransportManager::Instance().getItem(user_name);
        //TODO fix this poller is not current thread
        //CHECK(transport && transport->getPoller()->isCurrentThread());
        transport->setSession(shared_from_this());
        _transport = std::move(transport);
        InfoP(this);
    }
    _ticker.resetTime();
    CHECK(_transport);
    _transport->inputSockData(buffer->data() + 2, buffer->size() - 2, (struct sockaddr *)&_peer_addr);
}

void WebRtcSession::onError(const SockException &err) {
    //udp链接超时，但是rtc链接不一定超时，因为可能存在udp链接迁移的情况
    //在udp链接迁移时，新的WebRtcSession对象将接管WebRtcTransport对象的生命周期
    //本WebRtcSession对象将在超时后自动销毁
    WarnP(this) << err.what();

    if (!_transport) {
        return;
    }
    auto transport = std::move(_transport);
    getPoller()->async([transport] {
        //延时减引用，防止使用transport对象时，销毁对象
    }, false);
}

void WebRtcSession::onManager() {
    GET_CONFIG(float, timeoutSec, Rtc::kTimeOutSec);
    if (!_transport && _ticker.createdTime() > timeoutSec * 1000) {
        shutdown(SockException(Err_timeout, "illegal webrtc connection"));
        return;
    }
    if (_ticker.elapsedTime() > timeoutSec * 1000) {
        shutdown(SockException(Err_timeout, "webrtc connection timeout"));
        return;
    }
}

}// namespace mediakit


