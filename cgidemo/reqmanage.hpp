#ifndef AIBOX_CGIDEMO_REQMANAGE_HPP_
#define AIBOX_CGIDEMO_REQMANAGE_HPP_

#include <map>

#include "reqhandle.hpp"

class ReqManage {
 public:
  void RegistHttpHandle(std::string, HttpReqHandleBase*);
  std::string HandleRequest(const std::string& uriname, const std::string& reqbody);

 private:
  std::map<std::string, HttpReqHandleBase*> handlemap_;
};

#endif