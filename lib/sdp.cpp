#include "adts.h"
#include "defines.h"
#include "encode.h"
#include "h264.h"
#include "h265.h"
#include "http_parser.h"
#include "sdp.h"
#include "url.h"
#include "util.h"

//Dynamic types we hardcode:
//96 = AAC
//97 = H264
//98 = VP8
//99 = VP9
//100 = AC3
//101 = ALAW (non-standard properties)
//102 = opus
//103 = PCM (non-standard properties)
//104 = HEVC
//105 = ULAW (non-standard properties)
//106+ = unused

namespace SDP{

  static State *snglState = 0;
  static void snglStateInitCallback(const uint64_t track, const std::string &initData){
    snglState->updateInit(track, initData);
  }

  Track::Track(){
    rtcpSent = 0;
    channel = -1;
    firstTime = 0;
    packCount = 0;
    fpsTime = 0;
    fpsMeta = 0;
    fps = 0;
    mySSRC = rand();
    portA = portB = 0;
    cPortA = cPortB = 0;
  }

  /// Extracts a particular parameter from the fmtp string. fmtp member must be set before calling.
  std::string Track::getParamString(const std::string &param) const{
    if (!fmtp.size()){return "";}
    size_t pos = fmtp.find(param);
    if (pos == std::string::npos){return "";}
    pos += param.size() + 1;
    size_t ePos = fmtp.find_first_of(" ;", pos);
    return fmtp.substr(pos, ePos - pos);
  }

  /// Extracts a particular parameter from the fmtp string as an integer. fmtp member must be set
  /// before calling.
  uint64_t Track::getParamInt(const std::string &param) const{
    return atoll(getParamString(param).c_str());
  }

  /// Gets the SDP contents for sending out a particular given DTSC::Track.
  std::string mediaDescription(const DTSC::Meta *meta, size_t tid){
    const DTSC::Meta &M = *meta;
    std::stringstream mediaDesc;

    std::string codec = M.getCodec(tid);
    std::string init = M.getInit(tid);

    if (codec == "H264"){
      MP4::AVCC avccbox;
      avccbox.setPayload(init);
      mediaDesc << "m=video 0 RTP/AVP 97\r\n"
                   "a=rtpmap:97 H264/90000\r\n"
                   "a=cliprect:0,0,"
                << M.getHeight(tid) << "," << M.getWidth(tid) << "\r\na=framesize:97 "
                << M.getWidth(tid) << '-' << M.getHeight(tid)
                << "\r\n"
                   "a=fmtp:97 packetization-mode=1;profile-level-id="
                << std::hex << std::setw(2) << std::setfill('0') << (int)init.data()[1] << std::dec
                << "E0" << std::hex << std::setw(2) << std::setfill('0') << (int)init.data()[3]
                << std::dec << ";sprop-parameter-sets=";
      size_t count = avccbox.getSPSCount();
      for (size_t i = 0; i < count; ++i){
        mediaDesc << (i ? "," : "")
                  << Encodings::Base64::encode(std::string(avccbox.getSPS(i), avccbox.getSPSLen(i)));
      }
      mediaDesc << ",";
      count = avccbox.getPPSCount();
      for (size_t i = 0; i < count; ++i){
        mediaDesc << (i ? "," : "")
                  << Encodings::Base64::encode(std::string(avccbox.getPPS(i), avccbox.getPPSLen(i)));
      }
      mediaDesc << "\r\n"
                   "a=framerate:"
                << ((double)M.getFpks(tid)) / 1000.0 << "\r\na=control:track" << tid << "\r\n";
    }else if (codec == "HEVC"){
      h265::initData iData(init);
      mediaDesc << "m=video 0 RTP/AVP 104\r\n"
                   "a=rtpmap:104 H265/90000\r\n"
                   "a=cliprect:0,0,"
                << M.getHeight(tid) << "," << M.getWidth(tid) << "\r\na=framesize:104 "
                << M.getWidth(tid) << '-' << M.getHeight(tid) << "\r\n"
                << "a=fmtp:104 sprop-vps=";
      const std::set<std::string> &vps = iData.getVPS();
      if (vps.size()){
        for (std::set<std::string>::iterator it = vps.begin(); it != vps.end(); it++){
          if (it != vps.begin()){mediaDesc << ",";}
          mediaDesc << Encodings::Base64::encode(*it);
        }
      }
      mediaDesc << "; sprop-sps=";
      const std::set<std::string> &sps = iData.getSPS();
      if (sps.size()){
        for (std::set<std::string>::iterator it = sps.begin(); it != sps.end(); it++){
          if (it != sps.begin()){mediaDesc << ",";}
          mediaDesc << Encodings::Base64::encode(*it);
        }
      }
      mediaDesc << "; sprop-pps=";
      const std::set<std::string> &pps = iData.getPPS();
      if (pps.size()){
        for (std::set<std::string>::iterator it = pps.begin(); it != pps.end(); it++){
          if (it != pps.begin()){mediaDesc << ",";}
          mediaDesc << Encodings::Base64::encode(*it);
        }
      }
      mediaDesc << "\r\na=framerate:" << ((double)M.getFpks(tid)) / 1000.0 << "\r\na=control:track"
                << tid << "\r\n";
    }else if (codec == "VP8"){
      mediaDesc << "m=video 0 RTP/AVP 98\r\n"
                   "a=rtpmap:98 VP8/90000\r\n"
                   "a=cliprect:0,0,"
                << M.getHeight(tid) << "," << M.getWidth(tid) << "\r\na=framesize:98 "
                << M.getWidth(tid) << '-' << M.getHeight(tid) << "\r\n"
                << "a=framerate:" << ((double)M.getFpks(tid)) / 1000.0 << "\r\n"
                << "a=control:track" << tid << "\r\n";
    }else if (codec == "VP9"){
      mediaDesc << "m=video 0 RTP/AVP 99\r\n"
                   "a=rtpmap:99 VP8/90000\r\n"
                   "a=cliprect:0,0,"
                << M.getHeight(tid) << "," << M.getWidth(tid) << "\r\na=framesize:99 "
                << M.getWidth(tid) << '-' << M.getHeight(tid) << "\r\n"
                << "a=framerate:" << ((double)M.getFpks(tid)) / 1000.0 << "\r\n"
                << "a=control:track" << tid << "\r\n";
    }else if (codec == "MPEG2"){
      mediaDesc << "m=video 0 RTP/AVP 32\r\n"
                   "a=cliprect:0,0,"
                << M.getHeight(tid) << "," << M.getWidth(tid) << "\r\na=framesize:32 " << M.getWidth(tid)
                << '-' << M.getHeight(tid) << "\r\na=framerate:" << ((double)M.getFpks(tid)) / 1000.0
                << "\r\na=control:track" << tid << "\r\n";
    }else if (codec == "AAC"){
      mediaDesc << "m=audio 0 RTP/AVP 96"
                << "\r\n"
                   "a=rtpmap:96 mpeg4-generic/"
                << M.getRate(tid) << "/" << M.getChannels(tid)
                << "\r\n"
                   "a=fmtp:96 streamtype=5; profile-level-id=15; config=";
      for (unsigned int i = 0; i < init.size(); i++){
        mediaDesc << std::hex << std::setw(2) << std::setfill('0') << (int)init[i] << std::dec;
      }
      // these values are described in RFC 3640
      mediaDesc << "; mode=AAC-hbr; SizeLength=13; IndexLength=3; IndexDeltaLength=3;\r\n"
                   "a=control:track"
                << tid << "\r\n";
    }else if (codec == "MP3" || codec == "MP2"){
      mediaDesc << "m=" << M.getType(tid) << " 0 RTP/AVP 14"
                << "\r\n"
                   "a=rtpmap:14 MPA/90000/"
                << M.getChannels(tid) << "\r\n"
                << "a=control:track" << tid << "\r\n";
    }else if (codec == "AC3"){
      mediaDesc << "m=audio 0 RTP/AVP 100"
                << "\r\n"
                   "a=rtpmap:100 AC3/"
                << M.getRate(tid) << "/" << M.getChannels(tid) << "\r\n"
                << "a=control:track" << tid << "\r\n";
    }else if (codec == "ALAW"){
      if (M.getChannels(tid) == 1 && M.getRate(tid) == 8000){
        mediaDesc << "m=audio 0 RTP/AVP 8"
                  << "\r\n";
      }else{
        mediaDesc << "m=audio 0 RTP/AVP 101"
                  << "\r\n";
        mediaDesc << "a=rtpmap:101 PCMA/" << M.getRate(tid) << "/" << M.getChannels(tid) << "\r\n";
      }
      mediaDesc << "a=control:track" << tid << "\r\n";
    }else if (codec == "ULAW"){
      if (M.getChannels(tid) == 1 && M.getRate(tid) == 8000){
        mediaDesc << "m=audio 0 RTP/AVP 0"
                  << "\r\n";
      }else{
        mediaDesc << "m=audio 0 RTP/AVP 105"
                  << "\r\n";
        mediaDesc << "a=rtpmap:105 PCMU/" << M.getRate(tid) << "/" << M.getChannels(tid) << "\r\n";
      }
      mediaDesc << "a=control:track" << tid << "\r\n";
    }else if (codec == "PCM"){
      if (M.getSize(tid) == 16 && M.getChannels(tid) == 2 && M.getRate(tid) == 44100){
        mediaDesc << "m=audio 0 RTP/AVP 10"
                  << "\r\n";
      }else if (M.getSize(tid) == 16 && M.getChannels(tid) == 1 && M.getRate(tid) == 44100){
        mediaDesc << "m=audio 0 RTP/AVP 11"
                  << "\r\n";
      }else{
        mediaDesc << "m=audio 0 RTP/AVP 103"
                  << "\r\n";
        mediaDesc << "a=rtpmap:103 L" << M.getSize(tid) << "/" << M.getRate(tid) << "/"
                  << M.getChannels(tid) << "\r\n";
      }
      mediaDesc << "a=control:track" << tid << "\r\n";
    }else if (codec == "opus"){
      mediaDesc << "m=audio 0 RTP/AVP 102"
                << "\r\n"
                   "a=rtpmap:102 opus/"
                << M.getRate(tid) << "/" << M.getChannels(tid) << "\r\n"
                << "a=control:track" << tid << "\r\n";
    }
    return mediaDesc.str();
  }

  /// Generates a transport string suitable for in a SETUP request.
  /// By default generates a TCP mode string.
  /// Expects parseTransport to be called with the response from the server.
  std::string Track::generateTransport(uint32_t trackNo, const std::string &dest, bool TCPmode){
    if (TCPmode){
      // We simply request interleaved delivery over a trackNo-based identifier.
      // No need to set any internal state, parseTransport will handle it all.
      std::stringstream tStr;
      tStr << "RTP/AVP/TCP;unicast;interleaved=" << (trackNo * 2) << "-" << (trackNo * 2 + 1);
      return tStr.str();
    }else{
      // A little more tricky: we need to find free ports and remember them.
      data.SetDestination(dest, 1337);
      rtcp.SetDestination(dest, 1337);
      portA = portB = 0;
      int retries = 0;
      while (portB != portA + 1 && retries < 10){
        portA = data.bind(0);
        portB = rtcp.bind(portA + 1);
      }
      std::stringstream tStr;
      tStr << "RTP/AVP/UDP;unicast;client_port=" << portA << "-" << portB;
      return tStr.str();
    }
  }

  /// Sets the TCP/UDP connection details from a given transport string.
  /// Sets the transportString member to the current transport string on success.
  /// \param host The host connecting to us.
  /// \source The source identifier.
  /// \return True if successful, false otherwise.
  bool Track::parseTransport(const std::string &transport, const std::string &host,
                             const std::string &source, const DTSC::Meta *M, size_t tid){
    std::string codec = M->getCodec(tid);
    if (codec == "H264"){
      pack = RTP::Packet(97, 1, 0, mySSRC);
    }else if (codec == "HEVC"){
      pack = RTP::Packet(104, 1, 0, mySSRC);
    }else if (codec == "VP8"){
      pack = RTP::Packet(98, 1, 0, mySSRC);
    }else if (codec == "VP9"){
      pack = RTP::Packet(99, 1, 0, mySSRC);
    }else if (codec == "MPEG2"){
      pack = RTP::Packet(32, 1, 0, mySSRC);
    }else if (codec == "AAC"){
      pack = RTP::Packet(96, 1, 0, mySSRC);
    }else if (codec == "AC3"){
      pack = RTP::Packet(100, 1, 0, mySSRC);
    }else if (codec == "MP3" || codec == "MP2"){
      pack = RTP::Packet(14, 1, 0, mySSRC);
    }else if (codec == "ALAW"){
      if (M->getChannels(tid) == 1 && M->getRate(tid) == 8000){
        pack = RTP::Packet(8, 1, 0, mySSRC);
      }else{
        pack = RTP::Packet(101, 1, 0, mySSRC);
      }
    }else if (codec == "ULAW"){
      if (M->getChannels(tid) == 1 && M->getRate(tid) == 8000){
        pack = RTP::Packet(0, 1, 0, mySSRC);
      }else{
        pack = RTP::Packet(105, 1, 0, mySSRC);
      }
    }else if (codec == "PCM"){
      if (M->getSize(tid) == 16 && M->getChannels(tid) == 2 && M->getRate(tid) == 44100){
        pack = RTP::Packet(10, 1, 0, mySSRC);
      }else if (M->getSize(tid) == 16 && M->getChannels(tid) == 1 && M->getRate(tid) == 44100){
        pack = RTP::Packet(11, 1, 0, mySSRC);
      }else{
        pack = RTP::Packet(103, 1, 0, mySSRC);
      }
    }else if (codec == "opus"){
      pack = RTP::Packet(102, 1, 0, mySSRC);
    }else{
      ERROR_MSG("Unsupported codec %s for RTSP on track %zu", codec.c_str(), tid);
      return false;
    }
    if (transport.find("TCP") != std::string::npos){
      std::string chanE = transport.substr(transport.find("interleaved=") + 12,
                                           (transport.size() - transport.rfind('-') - 1)); // extract channel ID
      channel = atol(chanE.c_str());
      rtcpSent = 0;
      transportString = transport;
    }else{
      channel = -1;
      uint32_t sPortA = 0, sPortB = 0;
      cPortA = cPortB = 0;
      size_t sPort_loc = transport.rfind("server_port=") + 12;
      if (sPort_loc != std::string::npos){
        sPortA = atol(transport.substr(sPort_loc, transport.find('-', sPort_loc) - sPort_loc).c_str());
        sPortB = atol(transport.substr(transport.find('-', sPort_loc) + 1).c_str());
      }
      size_t port_loc = transport.rfind("client_port=") + 12;
      if (port_loc != std::string::npos){
        cPortA = atol(transport.substr(port_loc, transport.find('-', port_loc) - port_loc).c_str());
        cPortB = atol(transport.substr(transport.find('-', port_loc) + 1).c_str());
      }
      INFO_MSG("UDP ports: server %d/%d, client %d/%d", sPortA, sPortB, cPortA, cPortB);
      int sendbuff = 4 * 1024 * 1024;
      if (!sPortA || !sPortB){
        // Server mode - find server ports
        data.SetDestination(host, cPortA);
        setsockopt(data.getSock(), SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
        rtcp.SetDestination(host, cPortB);
        setsockopt(rtcp.getSock(), SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
        portA = data.bind(0);
        portB = rtcp.bind(0);
        std::stringstream tStr;
        tStr << "RTP/AVP/UDP;unicast;client_port=" << cPortA << '-' << cPortB << ";";
        if (source.size()){tStr << "source=" << source << ";";}
        tStr << "server_port=" << portA << "-" << portB << ";ssrc=" << std::hex << mySSRC << std::dec;
        transportString = tStr.str();
      }else{
        // Client mode - check ports and/or obey given ports if possible
        data.SetDestination(host, sPortA);
        setsockopt(data.getSock(), SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
        rtcp.SetDestination(host, sPortB);
        setsockopt(rtcp.getSock(), SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
        if (portA != cPortA){
          portA = data.bind(cPortA);
          if (portA != cPortA){
            FAIL_MSG("Server requested port %d, which we couldn't bind", cPortA);
            return false;
          }
        }
        if (portB != cPortB){
          portB = data.bind(cPortB);
          if (portB != cPortB){
            FAIL_MSG("Server requested port %d, which we couldn't bind", cPortB);
            return false;
          }
        }
        transportString = transport;
      }
      INFO_MSG("Transport string: %s", transportString.c_str());
    }
    return true;
  }

  /// Gets the rtpInfo for a given DTSC::Track, source identifier and timestamp (in millis).
  std::string Track::rtpInfo(const DTSC::Meta &M, size_t tid, const std::string &source, uint64_t currentTime){
    std::stringstream rInfo;
    rInfo << "url=" << source << "/track" << tid << ";"; // get the current url, not localhost
    rInfo << "seq=" << pack.getSequence() << ";rtptime=" << currentTime * getMultiplier(&M, tid);
    return rInfo.str();
  }

  State::State(){
    incomingPacketCallback = 0;
    myMeta = 0;
    snglState = this;
  }

  void State::parseSDP(const std::string &sdp){
    DONTEVEN_MSG("Parsing %zu-byte SDP", sdp.size());
    std::stringstream ss(sdp);
    std::string to;
    size_t tid = INVALID_TRACK_ID;
    bool nope = true; // true if we have no valid track to fill
    while (std::getline(ss, to, '\n')){
      if (!to.empty() && *to.rbegin() == '\r'){to.erase(to.size() - 1, 1);}
      if (to.empty()){continue;}
      DONTEVEN_MSG("Parsing SDP line: %s", to.c_str());

      // All tracks start with a media line
      if (to.substr(0, 2) == "m="){
        nope = true;
        tid = myMeta->addTrack();
        std::stringstream words(to.substr(2));
        std::string item;
        if (getline(words, item, ' ') && (item == "audio" || item == "video")){
          myMeta->setType(tid, item);
          myMeta->setID(tid, tid);
        }else{
          WARN_MSG("Media type not supported: %s", item.c_str());
          myMeta->removeTrack(tid);
          tracks.erase(tid);
          continue;
        }
        getline(words, item, ' ');
        if (!getline(words, item, ' ') || item.substr(0, 7) != "RTP/AVP"){
          WARN_MSG("Media transport not supported: %s", item.c_str());
          myMeta->removeTrack(tid);
          tracks.erase(tid);
          continue;
        }
        if (getline(words, item, ' ')){
          uint64_t avp_type = JSON::Value(item).asInt();
          switch (avp_type){
          case 0: // PCM Mu-law
            INFO_MSG("PCM Mu-law payload type");
            nope = false;
            myMeta->setCodec(tid, "ULAW");
            myMeta->setRate(tid, 8000);
            myMeta->setChannels(tid, 1);
            break;
          case 8: // PCM A-law
            INFO_MSG("PCM A-law payload type");
            nope = false;
            myMeta->setCodec(tid, "ALAW");
            myMeta->setRate(tid, 8000);
            myMeta->setChannels(tid, 1);
            break;
          case 10: // PCM Stereo, 44.1kHz
            INFO_MSG("Linear PCM stereo 44.1kHz payload type");
            nope = false;
            myMeta->setCodec(tid, "PCM");
            myMeta->setSize(tid, 16);
            myMeta->setRate(tid, 44100);
            myMeta->setChannels(tid, 2);
            break;
          case 11: // PCM Mono, 44.1kHz
            INFO_MSG("Linear PCM mono 44.1kHz payload type");
            nope = false;
            myMeta->setCodec(tid, "PCM");
            myMeta->setRate(tid, 44100);
            myMeta->setSize(tid, 16);
            myMeta->setChannels(tid, 1);
            break;
          case 14: // MPA
            INFO_MSG("MPA payload type");
            nope = false;
            myMeta->setCodec(tid, "MP3");
            myMeta->setRate(tid, 0);
            myMeta->setSize(tid, 0);
            myMeta->setChannels(tid, 0);
            break;
          case 32: // MPV
            INFO_MSG("MPV payload type");
            nope = false;
            myMeta->setCodec(tid, "MPEG2");
            break;
          default:
            // dynamic type
            if (avp_type >= 96 && avp_type <= 127){
              HIGH_MSG("Dynamic payload type (%" PRIu64 ") detected", avp_type);
              nope = false;
              continue;
            }else{
              FAIL_MSG("Payload type %" PRIu64 " not supported!", avp_type);
              myMeta->removeTrack(tid);
              tracks.erase(tid);
              continue;
            }
          }
        }
        tConv[tid].setProperties(*myMeta, tid);
        HIGH_MSG("Incoming track %s", myMeta->getTrackIdentifier(tid).c_str());
        continue;
      }

      if (nope){continue;}// ignore lines if we have no valid track
      // RTP mapping
      if (to.substr(0, 8) == "a=rtpmap"){
        std::string mediaType = to.substr(to.find(' ', 8) + 1);
        std::string trCodec = mediaType.substr(0, mediaType.find('/'));
        // convert to fullcaps
        for (unsigned int i = 0; i < trCodec.size(); ++i){
          if (trCodec[i] <= 122 && trCodec[i] >= 97){trCodec[i] -= 32;}
        }
        if (myMeta->getType(tid) == "audio"){
          std::string extraInfo = mediaType.substr(mediaType.find('/') + 1);
          if (extraInfo.find('/') != std::string::npos){
            size_t lastSlash = extraInfo.find('/');
            myMeta->setRate(tid, atoll(extraInfo.substr(0, lastSlash).c_str()));
            myMeta->setChannels(tid, atoll(extraInfo.substr(lastSlash + 1).c_str()));
          }else{
            myMeta->setRate(tid, atoll(extraInfo.c_str()));
            myMeta->setChannels(tid, 1);
          }
        }
        if (trCodec == "H264"){
          myMeta->setCodec(tid, "H264");
          myMeta->setRate(tid, 90000);
        }
        if (trCodec == "H265"){
          myMeta->setCodec(tid, "HEVC");
          myMeta->setRate(tid, 90000);
        }
        if (trCodec == "VP8"){
          myMeta->setCodec(tid, "VP8");
          myMeta->setRate(tid, 90000);
        }
        if (trCodec == "VP9"){
          myMeta->setCodec(tid, "VP9");
          myMeta->setRate(tid, 90000);
        }
        if (trCodec == "OPUS"){
          myMeta->setCodec(tid, "opus");
          myMeta->setInit(tid, "OpusHead\001\002\170\000\200\273\000\000\000\000\000", 19);
          myMeta->setRate(tid, 48000);
        }
        if (trCodec == "PCMA"){myMeta->setCodec(tid, "ALAW");}
        if (trCodec == "PCMU"){myMeta->setCodec(tid, "ULAW");}
        if (trCodec == "L8"){
          myMeta->setCodec(tid, "PCM");
          myMeta->setSize(tid, 8);
        }
        if (trCodec == "L16"){
          myMeta->setCodec(tid, "PCM");
          myMeta->setSize(tid, 16);
        }
        if (trCodec == "L20"){
          myMeta->setCodec(tid, "PCM");
          myMeta->setSize(tid, 20);
        }
        if (trCodec == "L24" || trCodec == "PCM"){
          myMeta->setCodec(tid, "PCM");
          myMeta->setSize(tid, 24);
        }
        if (trCodec == "MPEG4-GENERIC"){
          myMeta->setCodec(tid, "AAC");
          myMeta->setSize(tid, 16);
        }
        if (!myMeta->getCodec(tid).size()){
          ERROR_MSG("Unsupported RTP mapping: %s", mediaType.c_str());
        }else{
          tConv[tid].setProperties(*myMeta, tid);
          HIGH_MSG("Incoming track %s", myMeta->getTrackIdentifier(tid).c_str());
        }
        continue;
      }
      if (to.substr(0, 10) == "a=control:"){
        tracks[tid].control = to.substr(10);
        continue;
      }
      if (to.substr(0, 12) == "a=framerate:"){
        if (!myMeta->getRate(tid)){myMeta->setRate(tid, atof(to.c_str() + 12) * 1000);}
        continue;
      }
      if (to.substr(0, 12) == "a=framesize:"){
        // Ignored for now.
        /// \TODO Maybe implement?
        continue;
      }
      if (to.substr(0, 11) == "a=cliprect:"){
        // Ignored for now.
        /// \TODO Maybe implement?
        continue;
      }
      if (to.substr(0, 7) == "a=fmtp:"){
        tracks[tid].fmtp = to.substr(7);
        if (myMeta->getCodec(tid) == "AAC"){
          if (tracks[tid].getParamString("mode") != "AAC-hbr"){
            // a=fmtp:97
            // profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3;
            // config=120856E500
            FAIL_MSG("AAC transport mode not supported: %s", tracks[tid].getParamString("mode").c_str());
            nope = true;
            myMeta->removeTrack(tid);
            tracks.erase(tid);
            continue;
          }
          myMeta->setInit(tid, Encodings::Hex::decode(tracks[tid].getParamString("config")));
          // myMeta.tracks[trackNo].rate = aac::AudSpecConf::rate(myMeta.tracks[trackNo].init);
        }
        if (myMeta->getCodec(tid) == "H264"){
          // a=fmtp:96 packetization-mode=1;
          // sprop-parameter-sets=Z0LAHtkA2D3m//AUABqxAAADAAEAAAMAMg8WLkg=,aMuDyyA=;
          // profile-level-id=42C01E
          std::string sprop = tracks[tid].getParamString("sprop-parameter-sets");
          size_t comma = sprop.find(',');
          tracks[tid].spsData = Encodings::Base64::decode(sprop.substr(0, comma));
          tracks[tid].ppsData = Encodings::Base64::decode(sprop.substr(comma + 1));
          updateH264Init(tid);
        }
        if (myMeta->getCodec(tid) == "HEVC"){
          tracks[tid].hevcInfo.addUnit(
              Encodings::Base64::decode(tracks[tid].getParamString("sprop-vps")));
          tracks[tid].hevcInfo.addUnit(
              Encodings::Base64::decode(tracks[tid].getParamString("sprop-sps")));
          tracks[tid].hevcInfo.addUnit(
              Encodings::Base64::decode(tracks[tid].getParamString("sprop-pps")));
          updateH265Init(tid);
        }
        continue;
      }
      // We ignore bandwidth lines
      if (to.substr(0, 2) == "b="){continue;}
      // we ignore everything before the first media line.
      if (tid == INVALID_TRACK_ID){continue;}
      // at this point, the data is definitely for a track
      INFO_MSG("Unhandled SDP line for track %zu: %s", tid, to.c_str());
    }
    std::set<size_t> validTracks = myMeta->getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      INFO_MSG("Detected track %s", myMeta->getTrackIdentifier(*it).c_str());
    }
  }

  /// Calculates H265 track metadata from sps and pps data stored in tracks[trackNo]
  void State::updateH265Init(uint64_t tid){
    SDP::Track &RTrk = tracks[tid];
    if (!RTrk.hevcInfo.haveRequired()){
      MEDIUM_MSG("Aborted meta fill for hevc track %" PRIu64 ": no info nal unit", tid);
      return;
    }
    myMeta->setInit(tid, RTrk.hevcInfo.generateHVCC());

    h265::metaInfo MI = tracks[tid].hevcInfo.getMeta();

    RTrk.fpsMeta = MI.fps;
    myMeta->setWidth(tid, MI.width);
    myMeta->setHeight(tid, MI.height);
    myMeta->setFpks(tid, RTrk.fpsMeta * 1000);
    tConv[tid].setProperties(*myMeta, tid);
  }

  /// Calculates H264 track metadata from vps, sps and pps data stored in tracks[trackNo]
  void State::updateH264Init(uint64_t tid){
    SDP::Track &RTrk = tracks[tid];
    h264::sequenceParameterSet sps(RTrk.spsData.data(), RTrk.spsData.size());
    h264::SPSMeta hMeta = sps.getCharacteristics();
    MP4::AVCC avccBox;
    avccBox.setVersion(1);
    avccBox.setProfile(RTrk.spsData[1]);
    avccBox.setCompatibleProfiles(RTrk.spsData[2]);
    avccBox.setLevel(RTrk.spsData[3]);
    avccBox.setSPSCount(1);
    avccBox.setSPS(RTrk.spsData);
    avccBox.setPPSCount(1);
    avccBox.setPPS(RTrk.ppsData);
    RTrk.fpsMeta = hMeta.fps;
    myMeta->setWidth(tid, hMeta.width);
    myMeta->setHeight(tid, hMeta.height);
    myMeta->setFpks(tid, hMeta.fps * 1000);
    myMeta->setInit(tid, avccBox.payload(), avccBox.payloadSize());
    tConv[tid].setProperties(*myMeta, tid);
  }

  size_t State::getTrackNoForChannel(uint8_t chan){
    for (std::map<uint64_t, Track>::iterator it = tracks.begin(); it != tracks.end(); ++it){
      if (chan == it->second.channel){return it->first;}
    }
    return INVALID_TRACK_ID;
  }

  size_t State::parseSetup(HTTP::Parser &H, const std::string &cH, const std::string &src){
    static uint32_t trackCounter = 0;
    if (H.url == "200"){
      ++trackCounter;
      if (!tracks.count(trackCounter)){return INVALID_TRACK_ID;}
      if (!tracks[trackCounter].parseTransport(H.GetHeader("Transport"), cH, src, myMeta, trackCounter)){
        return INVALID_TRACK_ID;
      }
      return trackCounter;
    }

    HTTP::URL url(H.url);
    std::string urlString = url.getBareUrl();
    std::string pw = H.GetVar("pass");
    bool loop = true;

    while (loop){
      if (tracks.size()){
        for (std::map<uint64_t, Track>::iterator it = tracks.begin(); it != tracks.end(); ++it){
          if (!it->second.control.size()){
            it->second.control = "/track" + JSON::Value(it->first).asString();
            INFO_MSG("Control track: %s", it->second.control.c_str());
          }

          if ((urlString.size() >= it->second.control.size() &&
               urlString.substr(urlString.size() - it->second.control.size()) == it->second.control) ||
              (pw.size() >= it->second.control.size() &&
               pw.substr(pw.size() - it->second.control.size()) == it->second.control)){
            INFO_MSG("Parsing SETUP against track %" PRIu64, it->first);
            if (!it->second.parseTransport(H.GetHeader("Transport"), cH, src, myMeta, it->first)){
              return INVALID_TRACK_ID;
            }
            return it->first;
          }
        }
      }
      if (H.url.find("/track") != std::string::npos){
        uint32_t trackNo = atoi(H.url.c_str() + H.url.find("/track") + 6);
        // if (trackNo){
        INFO_MSG("Parsing SETUP against track %" PRIu32, trackNo);
        if (!tracks[trackNo].parseTransport(H.GetHeader("Transport"), cH, src, myMeta, trackNo)){
          return INVALID_TRACK_ID;
        }
        return trackNo;
        //}
      }
      if (urlString != url.path){
        urlString = url.path;
      }else{
        loop = false;
      }
    }
    return INVALID_TRACK_ID;
  }

  /// Returns the multiplier to use to get milliseconds from the RTP payload type for the given
  /// track
  double getMultiplier(const DTSC::Meta *M, size_t tid){
    if (M->getType(tid) == "video" || M->getCodec(tid) == "MP2" || M->getCodec(tid) == "MP3"){
      return 90.0;
    }
    if (M->getCodec(tid) == "opus"){
      return 48.0;
    }
    return ((double)M->getRate(tid) / 1000.0);
  }

  void State::updateInit(const uint64_t tid, const std::string &initData){
    myMeta->setInit(tid, initData.data(), initData.size());
  }

  /// Handles RTP packets generically, for both TCP and UDP-based connections.
  /// In case of UDP, expects packets to be pre-sorted.
  void State::handleIncomingRTP(const uint64_t track, const RTP::Packet &pkt){
    tConv[track].setCallbacks(incomingPacketCallback, snglStateInitCallback);
    tConv[track].addRTP(pkt);
  }

}// namespace SDP
