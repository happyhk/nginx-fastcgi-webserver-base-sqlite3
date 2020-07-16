
当遇到 bind failed:Address already in use时，直接列出已经绑定的端口号，并且删除绑定的进程即可：<br>
```Bash
netstat -tanlp <br>
sudo kill -9 [pid]<br>
```
