# Nginx+fastcgi搭建webserver

## 一、nginx的安装配置

1、下载nginx压缩包<br>

`wget https://nginx.org/download/nginx-1.14.0.tar.gz`<br>

2、解压下载好的nginx压缩包<br>

`tar -zxvf nginx-1.14.0.tar.gz`<br>

3、进入解压后的文件夹可以看到`configure*`文件，输入如下命令可以看到configure的一些参数：<br>

`./configure --help`

结果如图：<br>

![img](https://img-blog.csdn.net/20180929133735807?watermark/2/text/aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L1dhbGVzXzIwMTU=/font/5a6L5L2T/fontsize/400/fill/I0JBQkFCMA==/dissolve/70)

4、我这里把nginx的默认路径改为我安装的路径，所以依次执行以下命令安装nginx。<br>

`./configure --prefix=/home/huarui/server/nginx --conf-path=/home/huarui/server/nginx/nginx.conf`

`make`

`make install`

注：在执行`./configure`命令时出现以下错误：<br>

`error: the HTTP rewrite module requires the PCIRE library`

安装以来重新执行`./configure`即可，安装依赖命令`sudo apt-get install libpcre3 libpcre3-dev` <br>

5、安装成功后在nginx下运行启动命令，nginx即可运行，命令如下：<br>

`sudo ./sbin/nginx`

![img](https://img-blog.csdn.net/20180929144127669?watermark/2/text/aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L1dhbGVzXzIwMTU=/font/5a6L5L2T/fontsize/400/fill/I0JBQkFCMA==/dissolve/70)

## 二、spawn-fastcgi的安装

spawn-fastcgi的github: ` https://github.com/lighttpd/spawn-fcgi `<br>

这里使用的是1.6.3版本 ： `https://github.com/lighttpd/spawn-fcgi/releases/tag/v1.6.3`<br>

 如果没有`configure`，请先执行`./autogen.sh`，生成`configure`

 `./configure`

 `make`

编译好以后，将可执行文件移动到nginx的sbin目录下<br>

 `cp ./src/spawn-fcgi /usr/local/nginx/sbin/ `（cp到nginx的安装目录下）<br>

## 三、fastcgi库的安装

 访问`https://github.com/FastCGI-Archives/fcgi2`地址下载fastcgi库源码

 `./configure`，如果没有configure，请先执行`./autogen.sh`

  `make`

  `make install`

## 四、demo程序与发布测试

### 4.1 demo.c

```c
#include <fcgi_stdio.h>  
#include <unistd.h>
#include <stdlib.h>  
#include <string.h>

int main() {  
    int count = 0;  
    while (FCGI_Accept() >= 0) {  
        printf("Content-type: text/html\r\n"  
                "\r\n"  
                ""  
                "FastCGI Hello!"  
                "Request number %d running on host%s "  
                "Process ID: %d"
                "query_string:%s\n", ++count, getenv("SERVER_NAME"), getpid(),getenv("QUERY_STRING"));  
        char* method = getenv("REQUEST_METHOD");
        if(!strcmp(method, "POST")){
            int ilen = atoi(getenv("CONTENT_LENGTH"));
            char *bufp = malloc(ilen);
            fread(bufp, ilen,1,stdin);
            printf("THE POST data is<P>%s\n",bufp);
            printf("SCRIPT_FILENAME:%s\n",getenv("SCRIPT_FILENAME"));
            char* url_name = getenv("SCRIPT_FILENAME");
            char* str = (char *)malloc(strlen(url_name));
            int len = 0;
            for(int i = strlen(url_name)-1;i >= 0;i--){
                if(url_name[i] == '/') break;
                len++;
            }
            char tmp[len-1];
            int index = 0;
            for(int i = strlen(url_name)-1;i >= 0;i--){
                if(url_name[i] == '/') break;
                tmp[index++] = url_name[i];
            }
            int len1 = strlen(tmp);
            char* p = tmp;
            char* p1 = &tmp[len1-1];
            while(p < p1){
                char temp = *p;
                *p = *p1;
                *p1 = temp;
                p++;
                p1--;
            }
            char* ans = tmp;
            printf("The urllocation is : %s\n",ans);
            free(bufp);
        }
    }  
    return 0;  
} 
```

编译demo程序

`gcc demo.c -o demo -lfcgi`

直接运行可执行文件，看看能否正常运行。如果出现缺少库libfcgi.so.0，则自己需要手动把/usr/local/lib/libfcgi.so.0库建立一个链接到/usr/lib/目录下：<br>

`ln -s /usr/local/libfcgi.so.0 /usr/lib/`（或者把so的库路径添加到/etc/ld.so.conf，并执行ldconfig更新一下）<br>

### 4.2 发布demo程序

1）将CGI可执行程序移动到nginx的安装目录下 /usr/local/nginx/cgibin （文件夹不存在则自己创建）<br>

2）启动spawn-fcgi管理进程，并绑定server IP和端口（不要跟nginx的监听端口重合）<br>

`/usr/local/nginx/sbin/spawn-fcgi -a 172.10.10.228 -p 8088 -f /usr/local/nginx/cgibin/demo`

查看一下端口是否已经成功： `netstat -na | grep 8088`

3）更改nginx.conf配置文件，让nginx转发请求<br>

在http节点的子节点-"server节"点中下添加配置<br>

 ```Bash
  location ~ \.cgi$ {
  
    fastcgi_pass 127.0.0.1:8088;

    fastcgi_index index.cgi;

    fastcgi_param SCRIPT_FILENAME fcgi$fastcgi_script_name;

    include fastcgi_params;
}
 ```

 4）重启nginx或者重新加载配置文件<br>

 重新加载配置文件: `nginx -s reload`

 或者重启nginx  

 `killall nginx`

 `sudo ./nginx`

5）使用postman发送post请求的效果<br>

![img](https://github.com/happyhk/nginx-fastcgi-webserver-base-sqlite3/blob/master/images/Screenshot%20from%202020-07-17%2015-52-46.png)
