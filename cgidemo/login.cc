#include "login.hpp"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

bool LoginReq::ParseReq(const std::string& body) {
  // check valid http req
  rapidjson::Document document;
  document.Parse<0>(body.c_str());
  if (document.HasParseError()) {
    return false;
  }
  // check username and password exist
  if (!document.HasMember("username") || !document.HasMember("password"))
    return false;
  // get username and password, then check
  rapidjson::Value& val = document["username"];
  user_ = val.GetString();
  val = document["password"];
  passwd_ = val.GetString();
  // check user_ and passwd_
  if (user_.length() <= 0 || passwd_.length() <= 0) return false;
  return true;
}

std::string& LoginReq::SetRsp() {
  // echo the http req
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
  rapidjson::Value val1;
  rapidjson::Value val2;
  document.AddMember("username", val1.SetString(user_.c_str(), allocator), allocator);
  document.AddMember("password", val2.SetString(passwd_.c_str(), allocator), allocator);
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  rsp_ = buffer.GetString();
  return rsp_;
}