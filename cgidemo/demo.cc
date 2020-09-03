#include <string>
#include "httpdef.hpp"
#include "login.hpp"
#include "reqmanage.hpp"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcgi_stdio.h>

int main() {
  ReqManage manage;
  LoginReq loginreq;
  manage.RegistHttpHandle("login", &loginreq);       
  while (FCGI_Accept() >= 0) {
    printf("%s", HTTP_RESP_HEADER);
    std::string method = getenv("REQUEST_METHOD");
    // handle http post
    if (method == "POST") {
      // get http uri
      std::string uri = getenv("REQUEST_URI");
      // get last id, ex: /demo/login ==> login
      uri = uri.substr(uri.find_last_of('/') + 1);
      if (uri.length() == 0) {
        printf("%s", INVALID_RESP);
        continue;
      }
      // get request body
      std::string reqbody;
      int contentlen = atoi(getenv("CONTENT_LENGTH"));
      reqbody.reserve(contentlen);
      fread(&reqbody[0], contentlen, 1, stdin);      //读取body
      std::string rsp = manage.HandleRequest(uri, reqbody); //获取解析得到的body
      if (rsp.length() <= 0) {
        printf("%s", INVALID_RESP);
        continue;
      }
      printf("%s", rsp.c_str());
    }
  }
  return 0;
}