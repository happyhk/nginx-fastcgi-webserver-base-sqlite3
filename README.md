# nginx-fastcgi-webserver-base-sqlite3
搭建嵌入式的webserver，项目记录。
## sqllite 的基本使用
一、简介

sqlite3非常小，轻量级，就几百K大小；不需要用户名，密码，直接就可以对数据库进行操作。

二、安装sqlite3

1.安装sqlite3
```Bash
sudo apt-get install sqlite3
```
2.安装库文件
```Bash
sudo apt-get install libsqlite3-dev
```
不安装库文件的话，直接在C语言中包含头文件#include<sqlite3.h>的话，会报错，

三、sqlite3简单用法

[https://www.runoob.com/sqlite/sqlite-installation.html](https://www.runoob.com/sqlite/sqlite-installation.html)  

[https://blog.csdn.net/Gplusplus/article/details/52086443](https://blog.csdn.net/Gplusplus/article/details/52086443)
1.查看版本信息：sqlite3 -version <br>

2.进入sqlite3：直接输入sqlite3回车即可，和进入python一样<br>

3.退出sqlite3：.quit（sqlite比较独特的是，很多命令前都加了一个'.'）<br>

4.创建数据库：sqlite3 databasename.db(注意直接在shell中输入这条语句，不要在sqlite3环境中输入)<br>

创建的数据库名后面一班加上.db，它会在当前目录下创建一个数据库文件databasename.db<br>
好像创建数据库之后，得查看一下或者干点别的，不能直接.quit退出，不然会发现没有建立数据库文件。<br>

5.查看数据库列表：.databases<br>

6.删除数据库：直接把目录下的数据库文件删除就可以了。<br>

7.选择数据库：.open +数据库名<br>

这里如果数据库存在的话，就选择这个数据库；如果数据库不存在的话，系统会创建一个数据库test.db，然后选中这个数据库。<br>

8.创建表(需要先选中数据库)，和mysql中差不多<br>

CREATE TABLE Student(<br>

ID INT PRIMARY KEY NOT NULL,<br>

NAME VARCHAR(20),<br>

AGE INT);<br>


