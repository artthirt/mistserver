#include "output_ts_base.h"
#include <mist/ts_stream.h>
#include <mist/socket_srt.h>

namespace Mist{
  class OutTSSRT : public TSOutput{
  public:
    OutTSSRT(Socket::Connection &conn, Socket::SRTConnection & _srtSock);
    ~OutTSSRT();

    static bool listenMode(){return !(config->getString("target").size());}

    static void init(Util::Config *cfg);
    void sendTS(const char *tsData, size_t len = 188);
    bool isReadyForPlay(){return true;}
    virtual void requestHandler();
  protected:
    virtual void connStats(uint64_t now, Comms::Statistics &statComm);
    virtual std::string getConnectedHost(){return srtConn.remotehost;}
    virtual std::string getConnectedBinHost(){return srtConn.getBinHost();}

  private:
    HTTP::URL target;
    int64_t timeStampOffset;
    uint64_t lastTimeStamp;
    bool pushOut;
    Util::ResizeablePointer packetBuffer;
    Socket::UDPConnection pushSock;
    TS::Stream tsIn;
    TS::Assembler assembler;

    Socket::SRTConnection & srtConn;
  };
}// namespace Mist

typedef Mist::OutTSSRT mistOut;
