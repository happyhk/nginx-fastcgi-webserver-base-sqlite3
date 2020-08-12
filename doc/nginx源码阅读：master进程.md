# nginx源码阅读：master进程

## 前言

在上一个小节中,我们分析了一下nginx的初始化工作,在最后部分nginx进入多进程工作模式/单进程模式。单进程模式一般用于调试,实用性不高。<br>

本小节将分析master和worker进程所做的一些工作,比如master如何控制worker进程以及worker进程执行的流程。<br>

首先需要知道nginx由一个master进程管理多个worker进程,worker进程负责处理请求,而master负责做管理工作。nginx在main函数中最后调用`ngx_master_process_cycle(cycle)`进入多进程工作模式。接下来我们看看这个函数做了什么。<br>

### ngx_master_process_cycle

在进入到它的源码之前,我们需要知道不同的信号对nginx中的master进程的意义是什么。毕竟master进程的循环中主要就是靠信号改变标识位选择进行哪些操作。<br>

在上一个小节中,我们分析了一下nginx的初始化工作,在最后部分nginx进入多进程工作模式/单进程模式。单进程模式一般用于调试,实用性不高。<br>

本小节将分析master和worker进程所做的一些工作,比如master如何控制worker进程以及worker进程执行的流程。<br>

首先需要知道nginx由一个master进程管理多个worker进程,worker进程负责处理请求,而master负责做管理工作。nginx在main函数中最后调用`ngx_master_process_cycle(cycle)`进入多进程工作模式。接下来我们看看这个函数做了什么。<br>

### ngx_master_process_cycle

在进入到它的源码之前,我们需要知道不同的信号对nginx中的master进程的意义是什么。毕竟master进程的循环中主要就是靠信号改变标识位选择进行哪些操作。<br>

```c++
void
ngx_master_process_cycle(ngx_cycle_t *cycle)
{
    char              *title;
    u_char            *p;
    size_t             size;
    ngx_int_t          i;
    ngx_uint_t         n, sigio;
    sigset_t           set;
    struct itimerval   itv;
    ngx_uint_t         live;
    ngx_msec_t         delay;
    ngx_listening_t   *ls;
    ngx_core_conf_t   *ccf;

    /* 将各种信号加入信号集合
     * ngx_signal_value在 core/ngx_config.h中定义,为宏函数,原型如下:
     *      #define ngx_signal_helper(n)   SIG##n
     *      #define ngx_signal_value(n)    ngx_signal_helper(n)
     * 利用了宏的特殊用法,##代表在宏扩展时,宏参数会被直接替换到标示符中
     */
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGALRM);
    sigaddset(&set, SIGIO);
    sigaddset(&set, SIGINT);
    sigaddset(&set, ngx_signal_value(NGX_RECONFIGURE_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_REOPEN_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_NOACCEPT_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_TERMINATE_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_CHANGEBIN_SIGNAL));

    //将以上的信号全都设置屏蔽
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "sigprocmask() failed");
    }

    //清空信号集
    sigemptyset(&set);

    //master_process是一个字符串数组
    //定义:static u_char master_process[] = "master process"
    size = sizeof(master_process);

    //统计size大小
    //作为title(比如使用ps -u)最后一列显示command信息
    for (i = 0; i < ngx_argc; i++) {
        size += ngx_strlen(ngx_argv[i]) + 1;
    }

    //从内存池中申请空间
    title = ngx_pnalloc(cycle->pool, size);

    //将master_process字符数组拷贝到title中
    p = ngx_cpymem(title, master_process, sizeof(master_process) - 1);
    //将命令参数拷贝到title中
    for (i = 0; i < ngx_argc; i++) {
        *p++ = ' ';
        p = ngx_cpystrn(p, (u_char *) ngx_argv[i], size);
    }

    //设置进程名称为title,调用了setproctitle函数
    ngx_setproctitle(title);


    //获取存储ngx_core_module的配置项结构体指针
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    //启动工作进程
    //ccf->worker_processes表示工作进程的数量
    ngx_start_worker_processes(cycle, ccf->worker_processes,
                               NGX_PROCESS_RESPAWN);
    ngx_start_cache_manager_processes(cycle, 0);

    ngx_new_binary = 0;
    delay = 0;
    sigio = 0;
    //live表示是否有进程存活,如果没有则为0
    live = 1;

    for ( ;; ) {
        if (delay) {
            if (ngx_sigalrm) {
                sigio = 0;
                delay *= 2;
                ngx_sigalrm = 0;
            }

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "termination cycle: %d", delay);

            itv.it_interval.tv_sec = 0;
            itv.it_interval.tv_usec = 0;
            itv.it_value.tv_sec = delay / 1000;
            itv.it_value.tv_usec = (delay % 1000 ) * 1000;

            if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                              "setitimer() failed");
            }
        }

        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "sigsuspend");

        //阻塞等待信号发生
        sigsuspend(&set);

        /* 更新时间
         * nginx并不是每次都调用gettimeofday获取时间,因为系统调用太耗时
         * 所以使用了时间缓存
         * ngx_time_update更新时间缓存
         */
        ngx_time_update();

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "wake up, sigio %i", sigio);

        //ngx_reap标识位置位,证明有worker进程结束
        //此时需要重新启动一个worker进程
        if (ngx_reap) {
            ngx_reap = 0;
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "reap children");

            live = ngx_reap_children(cycle);
        }

        /* 没有进程存活或者ngx_terminate或ngx_quit置位
         * 则需要将master进程退出
         * 调用ngx_master_process_exit
         */
        if (!live && (ngx_terminate || ngx_quit)) {
            ngx_master_process_exit(cycle);
        }
        //强制退出
        if (ngx_terminate) {
            //延时若为0,设为50
            if (delay == 0) {
                delay = 50;
            }

            if (sigio) {
                sigio--;
                continue;
            }

            sigio = ccf->worker_processes + 2 /* cache processes */;

            if (delay > 1000) {
                ngx_signal_worker_processes(cycle, SIGKILL);
            } else {
                ngx_signal_worker_processes(cycle,
                                       ngx_signal_value(NGX_TERMINATE_SIGNAL));
            }

            continue;
        }
        //优雅的退出
        if (ngx_quit) {
            //通知工作进程退出
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
            //依次关闭监听的端口
            ls = cycle->listening.elts;
            for (n = 0; n < cycle->listening.nelts; n++) {
                if (ngx_close_socket(ls[n].fd) == -1) {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_socket_errno,
                                  ngx_close_socket_n " %V failed",
                                  &ls[n].addr_text);
                }
            }
            //将监听的端口总数置为0
            cycle->listening.nelts = 0;

            continue;
        }
        //若ngx_reconfigure置位,则重新加载配置
        if (ngx_reconfigure) {
            ngx_reconfigure = 0;

            if (ngx_new_binary) {
                ngx_start_worker_processes(cycle, ccf->worker_processes,
                                           NGX_PROCESS_RESPAWN);
                ngx_start_cache_manager_processes(cycle, 0);
                ngx_noaccepting = 0;

                continue;
            }

            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reconfiguring");
            //重新初始化
            cycle = ngx_init_cycle(cycle);
            if (cycle == NULL) {
                cycle = (ngx_cycle_t *) ngx_cycle;
                continue;
            }

            //重新获取存储ngx_core_module感兴趣的配置项结构体指针
            ngx_cycle = cycle;
            ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                   ngx_core_module);
            //开启worker进程
            ngx_start_worker_processes(cycle, ccf->worker_processes,
                                       NGX_PROCESS_JUST_RESPAWN);
            ngx_start_cache_manager_processes(cycle, 1);
            live = 1;
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
        }
        //重启标识位,跟信号无关
        if (ngx_restart) {
            ngx_restart = 0;
            ngx_start_worker_processes(cycle, ccf->worker_processes,
                                       NGX_PROCESS_RESPAWN);
            ngx_start_cache_manager_processes(cycle, 0);
            live = 1;
        }
        //ngx_reopen若置位,则重新打开所有文件
        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            //该函数在core/ngx_cycle.c中实现
            //就是将cycle结构体成员中的open_files单链表容器存储的文件重新打开
            ngx_reopen_files(cycle, ccf->user);
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_REOPEN_SIGNAL));
        }

        //ngx_change_binary,若置位,则平滑升级到新版本的nginx
        if (ngx_change_binary) {
            ngx_change_binary = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "changing binary");
            ngx_new_binary = ngx_exec_new_binary(cycle, ngx_argv);
        }

        //ngx_noaccept置位,代表所有worker进程不再接受新连接
        //其实也就意味着是对所有worker进程发送ngx_quit信号
        if (ngx_noaccept) {
            ngx_noaccept = 0;
            ngx_noaccepting = 1;
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
        }
    }
}
```

以上便是master进程做的工作，其实就是通过全局标示位来控制master的行为，在设置了信号屏蔽、申请内存池以及读取了ngx_core_module模块的配置之后，然后调用ngx_start_worker_processes函数根据设置的worker进程个数来启动worker进程。<br>

### ngx_start_worker_processes

该函数调用了ngx_spawn_process函数，而ngx_spawn_process中调用了fork函数创建worker进程。ngx_start_worker_processes函数的源码如下：<br>

```c++
/* os/unix/ngx_process_cycle.c */
static void
ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t n, ngx_int_t type)
{
    ngx_int_t      i;
    ngx_channel_t  ch;

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "start worker processes");

    //打开socketpair通信方式
    ch.command = NGX_CMD_OPEN_CHANNEL;

    for (i = 0; i < n; i++) {

        cpu_affinity = ngx_get_cpu_affinity(i);
        //真正启动工作进程的函数,里面调用了fork函数
        //ngx_worker_process_cycle即工作进程需要执行的操作
        ngx_spawn_process(cycle, ngx_worker_process_cycle, NULL,
                          "worker process", type);

        //设置ch
        ch.pid = ngx_processes[ngx_process_slot].pid;
        ch.slot = ngx_process_slot;
        ch.fd = ngx_processes[ngx_process_slot].channel[0];
        //传递消息
        ngx_pass_open_channel(cycle, &ch);
    }
}
```

nginx采用socketpair来完成进程间的通信，ngx_channel_t结构体是nginx定义的master父进程和worker子进程通信的消息格式，原型如下：<br>

```c++
typedef struct{  
        //传递的tcp消息中的命令  
        ngx_uint_t command;  
        //进程id,一般是发送方的进程id  
        ngx_pid_t pid;  
        //表示发送方在ngx_processes进程数组间的序号  
        ngx_int_t slot;  
        //通信的套接字句柄  
        ngx_fd_t fd;  
} ngx_channel_t;
```

command成员可以取值如下：<br>

```c++
//打开,使用这种方式通信前必须发送的命令
#define NGX_CMD_OPEN_CHANNEL 1
//关闭套接字
#define NGX_CMD_CLOSE_CHANNEL 2
//要求接收方正常退出进程
#define NGX_CMD_QUIT 3
//要求接收方强制结束进程
#define NGX_CMD_TERMINATE 4
//要求接收方重新打开进程已经打开过的文件
#define NGX_CMD_REOPEN 5
```

而关于ngx_processes数组，它里面存储了所有子进程相关的状态信息,便于`master`进程控制`worker`进程。它的定义以及`ngx_spawn_process`函数是如何实现的,我们下一节再分析。

### 小结

本小节分析了`master`进程所做的工作,初始化了之后,调用`sigsuspend`函数阻塞等待屏蔽信号集中的信号发生,根据信号来将相应的标识位置位,然后根据标识位选择不同的操作,大致的逻辑就这样,不过最好看着源码还有流程图加深一下理解。master进程通过socketpair与子进程进行通信,主要是对子进程的状态进行控制,还定义了全局数组`ngx_processes`,存储各子进程的状态信息。<br>

在下一小节中,我们分析`ngx_spawn_process`函数所做的工作,以及worker进程的工作循环。



