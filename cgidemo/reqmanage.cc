#include "reqmanage.hpp"

void ReqManage::RegistHttpHandle(std::string name, HttpReqHandleBase* handle) {
    handlemap_[name] = handle;
}

std::string ReqManage::HandleRequest(const std::string& uriname, const std::string& reqbody) {
    if (handlemap_.find(uriname) == handlemap_.end())
        return "";
    if (!handlemap_[uriname]->ParseReq(reqbody))
        return "";
    return handlemap_[uriname]->SetRsp();
}