# nginx源码阅读（5）：worker进程的工作循环

## 前言

在上一小节中,我们看到了nginx是如何使用master进程创建子进程,以及存储子进程的状态,也知道了进程间通信采用的是socketpair机制。接下来我们将重点分析`ngx_spawn_process`调用的`proc`函数,即worker进程的工作循环。

## ngx_worker_process_cycle

`proc`是一个函数指针,worker进程对应的是`ngx_worker_process_cycle`函数。<br>

worker进程跟master进程一样,也主要是通过信号改变标识位来决定运行逻辑,目前所用的具有实际意义的标识位对应的信号以及意义如下:<br>

ngx_worker_process_cycle对应的源码如下：<br>

```c++
static void
ngx_worker_process_cycle(ngx_cycle_t *cycle, void *data)
{
    ngx_uint_t         i;
    ngx_connection_t  *c;

    ngx_process = NGX_PROCESS_WORKER;

    //初始化worker进程
    ngx_worker_process_init(cycle, 1);

    //设置进程名
    ngx_setproctitle("worker process");

    //这里可以忽略掉,因为nginx采用的还是多进程单线程模式
#if (NGX_THREADS)
    {
    ngx_int_t         n;
    ngx_err_t         err;
    ngx_core_conf_t  *ccf;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (ngx_threads_n) {
        if (ngx_init_threads(ngx_threads_n, ccf->thread_stack_size, cycle)
            == NGX_ERROR)
        {
            /* fatal */
            exit(2);
        }

        err = ngx_thread_key_create(&ngx_core_tls_key);
        if (err != 0) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, err,
                          ngx_thread_key_create_n " failed");
            /* fatal */
            exit(2);
        }

        for (n = 0; n < ngx_threads_n; n++) {

            ngx_threads[n].cv = ngx_cond_init(cycle->log);

            if (ngx_threads[n].cv == NULL) {
                /* fatal */
                exit(2);
            }

            if (ngx_create_thread((ngx_tid_t *) &ngx_threads[n].tid,
                                  ngx_worker_thread_cycle,
                                  (void *) &ngx_threads[n], cycle->log)
                != 0)
            {
                /* fatal */
                exit(2);
            }
        }
    }
    }
#endif

    //根据标识位选择不同的操作
    for ( ;; ) {
        /* 若设置了退出标识位,则退出ngx_worker_process_cycle
         * 该标识位仅当ngx_worker_process_cycle退出时使用
         * 当ngx_quit被置位后,会将ngx_exiting置位
         * ngx_quit被置位后执行的那段代码并没有调用ngx_worker_process_exit函数
         * 而是放在这里执行
         */
        if (ngx_exiting) {

            c = cycle->connections;
            //将所有连接的关闭标识位置位
            //在退出前需要将所有读事件处理完
            for (i = 0; i < cycle->connection_n; i++) {

                /* THREAD: lock */

                if (c[i].fd != -1 && c[i].idle) {
                    c[i].close = 1;
                    c[i].read->handler(c[i].read);
                }
            }

            /* nginx管理定时事件是通过红黑数来存储,并集成到I/O复用机制上,具体的做法我们后面再分析
             * 这里的意思是没有定时事件时,则调用ngx_worker_process_exit将该worker进程退出
             * 否则还要把剩下的事件处理完
             */
            if (ngx_event_timer_rbtree.root == ngx_event_timer_rbtree.sentinel)
            {
                ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");

                ngx_worker_process_exit(cycle);
            }
        }

        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "worker cycle");

        //这个函数很重要,处理普通和定时事件的核心函数
        //我们在分析事件模块时会对其进行重点分析
        ngx_process_events_and_timers(cycle);

        //若强制退出进程的标识位置位
        //则直接调用ngx_worker_process_exit退出
        if (ngx_terminate) {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");

            ngx_worker_process_exit(cycle);
        }

        //该标识位表示优雅的关闭进程
        if (ngx_quit) {
            ngx_quit = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                          "gracefully shutting down");
            //设置进程的title
            ngx_setproctitle("worker process is shutting down");

            /* 若ngx_exiting没有置位
             * 则调用ngx_close_listening_sockets关闭所有监听的端口
             * 然后将ngx_exiting置位
             */
            if (!ngx_exiting) {
                ngx_close_listening_sockets(cycle);
                ngx_exiting = 1;
            }
        }

        //该标识位置位代表需要重新打开所有文件
        //调用ngx_reopen_files重新打开所有文件
        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, -1);
        }
    }
}
```

`ngx_worker_process_cycle`的逻辑还是比较清晰的,初始化了之后,执行死循环,不同的标识位置位后选择不同的操作,不过worker进程中并没有像master进程那样调用`sigsuspend`阻塞等待信号的发生(毕竟主要工作是处理事件)。<br>
接下来看看初始化工作进程的过程。<br>

## ngx_worker_process_init

初始化worker进程的函数为`ngx_worker_process_init`,<br>

```c++
static void
ngx_worker_process_init(ngx_cycle_t *cycle, ngx_int_t worker)
{
    sigset_t          set;
    uint64_t          cpu_affinity;
    ngx_int_t         n;
    ngx_uint_t        i;
    struct rlimit     rlmt;
    ngx_core_conf_t  *ccf;
    ngx_listening_t  *ls;

      /* 设置全局的环境变量environ */
    if (ngx_set_environment(cycle, NULL) == NULL) {
        /* fatal */
        exit(2);
    }

      /* 获取ngx_core_module模块的配置 */
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

      /* 设置worker进程优先级 */
    if (worker >= 0 && ccf->priority != 0) {
        if (setpriority(PRIO_PROCESS, 0, ccf->priority) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setpriority(%d) failed", ccf->priority);
        }
    }

      /* 调用setrlimit系统调用限制成员资源的使用 */
    if (ccf->rlimit_nofile != NGX_CONF_UNSET) {
        rlmt.rlim_cur = (rlim_t) ccf->rlimit_nofile;
        rlmt.rlim_max = (rlim_t) ccf->rlimit_nofile;

        if (setrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setrlimit(RLIMIT_NOFILE, %i) failed",
                          ccf->rlimit_nofile);
        }
    }

    if (ccf->rlimit_core != NGX_CONF_UNSET) {
        rlmt.rlim_cur = (rlim_t) ccf->rlimit_core;
        rlmt.rlim_max = (rlim_t) ccf->rlimit_core;

        if (setrlimit(RLIMIT_CORE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setrlimit(RLIMIT_CORE, %O) failed",
                          ccf->rlimit_core);
        }
    }

#ifdef RLIMIT_SIGPENDING
    if (ccf->rlimit_sigpending != NGX_CONF_UNSET) {
        rlmt.rlim_cur = (rlim_t) ccf->rlimit_sigpending;
        rlmt.rlim_max = (rlim_t) ccf->rlimit_sigpending;

        if (setrlimit(RLIMIT_SIGPENDING, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setrlimit(RLIMIT_SIGPENDING, %i) failed",
                          ccf->rlimit_sigpending);
        }
    }
#endif

    /* geteuid()用于获取执行目前进程有效的用户id,root用户的uid为0
     * 这里即是指若用户为root用户
     */
    if (geteuid() == 0) {
        /* 设置组ID */
        if (setgid(ccf->group) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "setgid(%d) failed", ccf->group);
            /* fatal */
            exit(2);
        }

        /* 在user中设置组ID */
        if (initgroups(ccf->username, ccf->group) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "initgroups(%s, %d) failed",
                          ccf->username, ccf->group);
        }

        /* 将worker进程设置为拥有该文件所有者同样的权限 */
        if (setuid(ccf->user) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "setuid(%d) failed", ccf->user);
            /* fatal */
            exit(2);
        }
    }

    /* 根据配置文件决定是否进行cpu与进程的绑定 */
    if (worker >= 0) {
        cpu_affinity = ngx_get_cpu_affinity(worker);

        if (cpu_affinity) {
            ngx_setaffinity(cpu_affinity, cycle->log);
        }
    }

#if (NGX_H***E_PR_SET_DUMPABLE)

    /* allow coredump after setuid() in Linux 2.4.x */

    if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "prctl(PR_SET_DUMPABLE) failed");
    }

#endif

    /* 更改工作路径 */
    if (ccf->working_directory.len) {
        if (chdir((char *) ccf->working_directory.data) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "chdir(\"%s\") failed", ccf->working_directory.data);
            /* fatal */
            exit(2);
        }
    }

    /* 初始化信号集 */
    sigemptyset(&set);

    /* 这一步的操作意味着清空进程的阻塞信号集 */
    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "sigprocmask() failed");
    }

    /*
     * disable deleting previous events for the listening sockets because
     * in the worker processes there are no events at all at this point
     */

    /* 初始化worker进程所有的监听端口 */
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        ls[i].previous = NULL;
    }

    /* 
     * worker进程进行所有模块的自定义初始化
     * 调用每个模块实现的init_process方法
     */
    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->init_process) {
            if (ngx_modules[i]->init_process(cycle) == NGX_ERROR) {
                /* fatal */
                exit(2);
            }
        }
    }   
    /* 
     * ngx_processes[]是所有进程共享的全局变量
     * 为了保证当前worker进程禁止读取master进程给其他worker进程的消息,因此需要关闭其他worker进程的读端
     * 并保留对其他worker进程的写端和自身的读端,这是为了能够保持所有worker进程间的通信,即通过其他worker进程写端写入消息,通过读端来接收来自master和其他worker进程的消息
     */
    for (n = 0; n < ngx_last_process; n++) {

        /* 跳过不存在的worker进程 */
        if (ngx_processes[n]
.pid == -1) {
            continue;
        }

        /* 跳过该worker进程 */
        if (n == ngx_process_slot) {
            continue;
        }

        /* 如果读端channel[1]已关闭,跳过 */
        if (ngx_processes[n]
.channel[1] == -1) {
            continue;
        }

        /* 关闭对其他worker进程的读端文件描述符,保留写端文件描述符用于worker间的通信之用 */
        if (close(ngx_processes[n]
.channel[1]) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "close() channel failed");
        }
    }

    /* 
     * 关闭自身的写端文件描述符,因为每个worker进程只需要从读端读取消息,而不用给自己写消息
     * 需要用到当前进程的写端文件描述符的是master以及其他的worker进程
     * 其实这一系列操作都是为了关闭不必要的channel
     */
    if (close(ngx_processes[ngx_process_slot].channel[0]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() channel failed");
    }

#if 0
    ngx_last_process = 0;
#endif

    /* 根据全局变量ngx_channel开启一个通道,只用于处理读事件(ngx_channel_handler) */
    if (ngx_add_channel_event(cycle, ngx_channel, NGX_READ_EVENT,
                              ngx_channel_handler)
        == NGX_ERROR)
    {
        /* fatal */
        exit(2);
    }
}
```

worker进程初始化所做的工作主要的步骤总结如下:<br>

1. 设置ngx_process = NGX_PROCESS_WORKER,在master进程中这个变量被设置为NGX_PROCESS_MASTER;
2. 根据配置信息设置执行环境、优先级、资源限制、setgid、setuid、更改工作路径、信号初始化等;<br>
3. 调用所有模块的init_process方法;<br>
4. 关闭不使用的socket:关闭当前worker进程的写端以及其他worker的读端,当前worker进程可以使用其他worker的写端发送消息,使用当前worker进程的读端监听可读事件。<br>

## 小结

本小结中主要分析了worker进程执行的工作循环,首先worker进程进行初始化,接着开始执行死循环,根据不同的标识位选择不同的操作。在阅读源码的过程中,建议不要一来就陷入细节,而是应该把握主体关键代码,到后面熟悉各方面的功能之后,再进行细读。<br>

之前在master进程中已经使用了内存池进行空间的分配,接下来我们将对nginx的内存池的实现进行分析。<br>















