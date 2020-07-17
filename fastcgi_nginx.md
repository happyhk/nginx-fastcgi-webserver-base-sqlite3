##  nginx的安装与配置

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

