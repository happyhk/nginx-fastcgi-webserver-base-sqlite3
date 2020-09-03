#ifndef AIBOX_CGIDEMO_REQHANDLE_HPP_
#define AIBOX_CGIDEMO_REQHANDLE_HPP_

#include <string>

// base class for http post handle
class HttpReqHandleBase {
 public:
  // parse http request body
  virtual bool ParseReq(const std::string& body) = 0;
  virtual std::string& SetRsp() = 0;
};

#endif