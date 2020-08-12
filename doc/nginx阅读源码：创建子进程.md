# nginx阅读源码：创建子进程

## 前言

在上一小节中,我们主要分析了`master`进程的工作循环。本小结中,我们将看到nginx是如何创建worker进程的。在上一小节中分析`master`进程的工作循环时,调用了`ngx_start_worker_processes`函数,但是其内部调用的创建子进程的代码在`ngx_spawn_process`中。

## ngx_spawn_process

创建进程自然想到的就是调用`fork`函数。`ngx_spawn_process`函数中调用了`fork`来完成对子进程的创建。
先分析一下该函数参数:
\1. `ngx_cycle_t *cycle`:给子进程的

1. `ngx_spawn_proc_pt proc`:函数指针,定义为:`typedef void (*ngx_spawn_proc_pt) (ngx_cycle_t *cycle, void *data);`。proc函数指针指向worker进程要执行的工作循环。
2. `void *data`:proc回调函数的参数
3. `char *name`:进程的名字,worker进程对应的是`worker process`
4. `ngx_int_t respawn`:创建子进程时的属性,目前可以的取值为:
   `//子进程退出时,父进程不会再次创建(在创建cache loader process时使用)#define NGX_PROCESS_NORESPAWN -1//区别旧/新进程的标识位#define NGX_PROCESS_JUST_SPAWN -2//子进程异常退出时,父进程重新生成子进程的标识位#define NGX_PROCESS_RESPAWN -3//区别旧/新进程的标识位#define NGX_PROCESS_JUST_RESPAWN -4//热代码替换,父、子进程分离的标识位#define NGX_PROCESS_DETACHED -5`

`ngx_spawn_process`的源码如下:

```c++
ngx_pid_t
ngx_spawn_process(ngx_cycle_t *cycle, ngx_spawn_proc_pt proc, void *data,
    char *name, ngx_int_t respawn)
{
    u_long     on;
    ngx_pid_t  pid;
    ngx_int_t  s;   //s为创建进程时,在全局数组ngx_processes的下标

    //若大于0,则代表respawn下标对应的进程需要重启
    if (respawn >= 0) {
        s = respawn;

    } else {
        //在ngx_processes数组中找到一个空位用于启动进程
        for (s = 0; s < ngx_last_process; s++) {
            if (ngx_processes[s].pid == -1) {
                break;
            }
        }
        //NGX_MAX_PROCESSES宏定义为1024,代表可创建的进程最大数
        if (s == NGX_MAX_PROCESSES) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "no more than %d processes can be spawned",
                          NGX_MAX_PROCESSES);
            return NGX_INVALID_PID;
        }
    }

    //不是热代码替换
    if (respawn != NGX_PROCESS_DETACHED) {

        /* Solaris 9 still has no AF_LOCAL */
        //调用socketpair为新的worker进程创建一对socket,后面会用于进程间通信
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, ngx_processes[s].channel) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "socketpair() failed while spawning \"%s\"", name);
            return NGX_INVALID_PID;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "channel %d:%d",
                       ngx_processes[s].channel[0],
                       ngx_processes[s].channel[1]);

        /* #define ngx_nonblocking(s) fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK) */
        //将channel[0]设置非阻塞,给父进程使用
        if (ngx_nonblocking(ngx_processes[s].channel[0]) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_nonblocking_n " failed while spawning \"%s\"",
                          name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }
        //将channel[1]设置非阻塞,channel[1]给子进程使用
        if (ngx_nonblocking(ngx_processes[s].channel[1]) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_nonblocking_n " failed while spawning \"%s\"",
                          name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }

        /* 设置channel[0]的信号驱动异步I/O标志
         * FIOASYNC：该状态标志决定是否收取针对socket的异步I/O信号（SIGIO）
         * 与O_ASYNC文件状态标志等效，可通过fcntl的F_SETFL命令设置或者清除
         */
        on = 1;
        if (ioctl(ngx_processes[s].channel[0], FIOASYNC, &on) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "ioctl(FIOASYNC) failed while spawning \"%s\"", name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }

        /* F_SETOWN：用于指定接收SIGIO和SIGURG信号的socket属主（进程ID或进程组ID）
         * 这里意思是指定master进程接收SIGIO和SIGURG信号
         * SIGIO信号必须是在socket设置为信号驱动异步I/O才能产生，即上一步操作
         * SIGURG信号是在新的带外数据到达socket时产生的
         */
        if (fcntl(ngx_processes[s].channel[0], F_SETOWN, ngx_pid) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "fcntl(F_SETOWN) failed while spawning \"%s\"", name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }

        /* FD_CLOEXEC：用来设置文件的close-on-exec状态
             * 在调用了exec()函数簇中某个函数之后,close-on-exec标志为0的情况下(默认),打开的文件描述符会被继承
         * 设置了之后打开的文件描述符会被关闭
         */
        if (fcntl(ngx_processes[s].channel[0], F_SETFD, FD_CLOEXEC) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "fcntl(FD_CLOEXEC) failed while spawning \"%s\"",
                           name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }

        if (fcntl(ngx_processes[s].channel[1], F_SETFD, FD_CLOEXEC) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "fcntl(FD_CLOEXEC) failed while spawning \"%s\"",
                           name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }

        //设置当前子进程的socket,父进程对其进行监听
        ngx_channel = ngx_processes[s].channel[1];

    } else {
        ngx_processes[s].channel[0] = -1;
        ngx_processes[s].channel[1] = -1;
    }

    //设置下标值,在调用`ngx_pass_open_channel()`函数父进程给子进程传递信息时使用
    ngx_process_slot = s;

    //创建子进程
    pid = fork();

    switch (pid) {
    //出错
    case -1:
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "fork() failed while spawning \"%s\"", name);
        ngx_close_channel(ngx_processes[s].channel, cycle->log);
        return NGX_INVALID_PID;
    //子进程
    case 0:
        //获取子进程的id号
        ngx_pid = ngx_getpid();
        //调用子进程的工作循环
        proc(cycle, data);
        break;

    default:
        break;
    }

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "start %s %P", name, pid);

    //设置全局数组中该进程的信息
    ngx_processes[s].pid = pid;
    ngx_processes[s].exited = 0;

    //若respawn大于0,代表重启某个进程完成
    //其他的信息不需要更改,所以直接返回
    if (respawn >= 0) {
        return pid;
    }

    //否则要更新信息
    ngx_processes[s].proc = proc;
    ngx_processes[s].data = data;
    ngx_processes[s].name = name;
    ngx_processes[s].exiting = 0;

    //根据传入的不同属性
    //将子进程对应的标识位置位
    //上面已经讲解过标识位的意义,这里就不重复了
    switch (respawn) {

    case NGX_PROCESS_NORESPAWN:
        ngx_processes[s].respawn = 0;
        ngx_processes[s].just_spawn = 0;
        ngx_processes[s].detached = 0;
        break;

    case NGX_PROCESS_JUST_SPAWN:
        ngx_processes[s].respawn = 0;
        ngx_processes[s].just_spawn = 1;
        ngx_processes[s].detached = 0;
        break;

    case NGX_PROCESS_RESPAWN:
        ngx_processes[s].respawn = 1;
        ngx_processes[s].just_spawn = 0;
        ngx_processes[s].detached = 0;
        break;

    case NGX_PROCESS_JUST_RESPAWN:
        ngx_processes[s].respawn = 1;
        ngx_processes[s].just_spawn = 1;
        ngx_processes[s].detached = 0;
        break;

    case NGX_PROCESS_DETACHED:
        ngx_processes[s].respawn = 0;
        ngx_processes[s].just_spawn = 0;
        ngx_processes[s].detached = 1;
        break;
    }

    //ngx_last_process代表ngx_processes数组中占用了的元素个数
    if (s == ngx_last_process) {
        ngx_last_process++;
    }

    return pid;
}
```

从该函数可以看出,master进程通过socketpair机制和子进程进行通信,并且使用了全局数组`ngx_processes`来存储子进程的信息,并设置子进程的属性(是否重启等)。关于`ngx_processes`数组等的全局变量定义如下:<br>

```c++
//最多只能有1024个进程
#define NGX_MAX_PROCESSES 1024
//当前操作的进程在ngx_processes数组中的下标
ngx_int_t ngx_process_slot;
//ngx_processes数组中有意义的ngx_processes_t元素中最大的下标
ngx_int_t ngx_last_process;
//存储所有子进程信息的数组
ngx_process_t ngx_processes[NGX_MAX_PROCESSES];
```

## ngx_process_t结构体

关于`ngx_process_t`结构体定义如下:<br>

```c++
typedef struct {
  //进程的id号
  ngx_pid_t pid;
  //通过waitpid系统调用获取到的进程状态
  int status;
  //通过socketpair系统调用产生的用于进程间通信的一对socket,用于相互通信。
  ngx_socket_t channel[2];
  //子进程的工作循环
  ngx_spawn_proc_pt proc;
  //proc的第二个参数,可能需要传入一些数据
  void *data;
  //进程名称
  char *name;

  //一些标识位
  //为1时代表需要重新生成子进程
  unsigned respawn : 1;
  //为1代表需要生成子进程
  unsigned just_spawn : 1;
  //为1代表需要进行父、子进程分离
  unsigned detached : 1;
  //为1代表进程正在退出
  unsigned exiting : 1;
  //为1代表进程已经退出了
  unsigned exited : 1;
} ngx_process_t;
```

#### 实现子进程间的通信

当每个子进程创建完成后,会向其他进程发送自己的信息,利用这一点,就可以完成任意子进程之间的通信了。<br>

至于为什么要这样做,这个地方是值得推敲的。我们都知道父进程调用`fork`创建子进程时,子进程会继承父进程打开的文件描述符。在nginx中master进程调用`socketpair`创建了一对UNIX域套结字,随后调用`fork`创建子进程。这样master进程可以通过向`channel[0]`读写数据,而子进程通过`channel[1]`来发收数据,达到了父子进程间通信。但是当子进程之间想通信时,情况就变的复杂一些了。<br>

此时分为两种情况:第一种情况是`ngx_processes`数组中排在后面的进程向前面进程发送数据;第二种情况是排在前面的进程向后面的进程发送数据。<br>
第一种情况很好解决,由于master的`ngx_processes`数组中保存了与每个子进程进行通信的套结字对,因此只要后面的进程知道前面进程在`ngx_processes`中的下标,然后对其的`channel[0]`发数据,这样前面的进程就可以收到数据了。<br>

例如,此时有master进程A,2个worker进程B和C,进程A与进程B之间使用3、4描述符通信,进程A与进程C之间使用5、6描述符通信(master进程均用较小的描述符收发B、C进程的数据,即3、5)。C要想向B发送数据,由于子进程会继承父进程打开的文件描述符,因此C进程的3、4描述符是打开的并且用于A、B之间通信,所以我们只需要向3号描述符写数据,B进程就可以通过4号描述符获得数据。<br>

第二种情况要复杂很多,还是以A、B、C进程为例,此时B想向C发数据,通过5号描述符是否可行？答案是肯定不行,因为在B进程中,5号描述符并不是用于A和C进程通信的,它甚至可能还未被使用(B比C先创建,没有继承5、6号描述符)。很明显,我们需要获得A与C之间的`channel[0]`在进程B中对应的文件描述符是多少,通过进程之间传递文件描述符即可解决(注意传递的不是描述符这个数字,而是使得两个进程的文件描述符获取的文件对象为同一个`strcut file*`)。<br>

因此,在nginx中,每创建一个子进程,就向`ngx_processes`中有效的进程发送新创建的子进程的信息(发送的操作对应的函数是`ngx_pass_open_channel`),传递的信息对应的数据结构是`ngx_channel_t`,它的组成在上一小节已经给出,其中包含了子进程的pid以及它的`channel[0]`还有下标等信息,通过在进程间传递文件描述符,我们最终就可以实现子进程与子进程间通信(虽然目前nginx并没有使用这种方法进行子进程之间的通信,但是若以后扩展会很容易)。<br>

最后再结合一下之前的例子整理一下思路,当A创建了进程C时,A进程会通过3号文件描述符向B进程传递以`ngx_channel_t`为数据结构的信息,其中包含了需要传递的5号文件描述符,这样B进程收到数据之后(**在nginx中,B进程的`channel[1]`在`ngx_worker_process_init`中已经通过`ngx_add_channel_event`添加为读事件,因此A向B写数据,该读事件(`ngx_channel_handler`会被回调)会被触发,进而处理数据**),会创建一个新的文件描述符指向进程A、C中5号文件描述符一样的`struct file*`,此时B便可以利用这个新的文件描述符与C进程通信。(至于进程间如何传递文件描述符,网上有很多资料,APUE中也有写,这里就不分析了,以免篇幅太长不利于阅读)<br>

接下来展开`ngx_pass_open_channel`的源码进行分析,看看是如何传递消息的。<br>

```c++
static void
ngx_pass_open_channel(ngx_cycle_t *cycle, ngx_channel_t *ch)
{
    ngx_int_t  i;

       /* 遍历从开始直到最后一个存储在ngx_processes数组中有意义的进程 */
    for (i = 0; i < ngx_last_process; i++) {

            // 跳过当前进程以及不存在的子进程以及其父进程socket关闭的子进程
        if (i == ngx_process_slot
            || ngx_processes[i].pid == -1
            || ngx_processes[i].channel[0] == -1)
        {
            continue;
        }

        ngx_log_debug6(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                      "pass channel s:%d pid:%P fd:%d to s:%i pid:%P fd:%d",
                      ch->slot, ch->pid, ch->fd,
                      i, ngx_processes[i].pid,
                      ngx_processes[i].channel[0]);

        /* TODO: NGX_AGAIN */

            /* 
             * 给每个进程的父进程发送刚创建的进程的信息,然后该进程的读事件就会被触发,从而获取该进程的信息
             * ngx_write_channel中实现了不同进程间文件描述符的传递中的发送文件描述符
             */
        ngx_write_channel(ngx_processes[i].channel[0],
                          ch, sizeof(ngx_channel_t), cycle->log);
    }
}
```

## 小结

本小节分析了master进程创建子进程的代码。`ngx_spawn_process`时传入的`proc`参数决定了子进程的工作循环,最后一个参数决定了创建子进程的属性(是否重启子进程,以及是新/旧子进程等)。其实就是保证父子进程间可以通信的同时让子进程执行自己的工作循环,关于子进程的信息存储在`ngx_processes`全局数组中,可以联系上小节一起看一下,形成一个整体的流程。<br>

下小节中我们将看到worker进程执行的工作循环是怎样的。<br>

本小节分析了master进程创建子进程的代码。`ngx_spawn_process`时传入的`proc`参数决定了子进程的工作循环,最后一个参数决定了创建子进程的属性(是否重启子进程,以及是新/旧子进程等)。其实就是保证父子进程间可以通信的同时让子进程执行自己的工作循环,关于子进程的信息存储在`ngx_processes`全局数组中,可以联系上小节一起看一下,形成一个整体的流程。<br>

下小节中我们将看到worker进程执行的工作循环是怎样的。<br>











