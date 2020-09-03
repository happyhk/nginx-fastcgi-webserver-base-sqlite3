#ifndef AIBOX_CGIDEMO_LOGIN_HPP_
#define AIBOX_CGIDEMO_LOGIN_HPP_

#include "reqhandle.hpp"

class LoginReq : public HttpReqHandleBase {
 public:
  virtual bool ParseReq(const std::string& body);
  virtual std::string& SetRsp();

 private:
  std::string rsp_;
  std::string user_;
  std::string passwd_;
};

#endif