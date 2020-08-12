# nginx源码阅读（6）：内存池

## 前言

本小节中,我们将看到内存池的实现。由于nginx是由c语言实现,并没有垃圾回收机制,比较容易导致memory leak,因此nginx实现了自己的内存池,将内存的管理和释放交给内存池,而需要申请内存的地方直接使用内存池来申请内存即可,每个连接/请求都会新建一个内存池。<br>

既然释放内存是由内存池来释放,那么何时释放也是一个问题,nginx中的做法是在内存池销毁的时候也将内存释放了,这样可行是因为nginx是一个web服务器,每个请求的生存周期都比较短暂,不会造成一个内存池申请了大量的内存但是本来早就可以释放了却因为生命周期比较长,造成内存得不到及时释放。

## 内存池相关的结构体

nginx的内存池根据申请的内存是大块还是小块有不同的处理机制。而内存池相关的结构体在`src/core/ngx_palloc.h`中定义。<br>

管理内存池的核心结构体`struct ngx_pool_s`的定义如下:<br>

```c++
typedef struct ngx_pool_s         ngx_pool_t;

struct ngx_pool_s {
  /* 管理小块的内存池。
   * 当该内存池中的空间不足时,会再分配一个ngx_pool_data_t,并使用ngx_pool_data_t结构体中的成员next连接起来
   * 最终形成一个单链表
   */
  ngx_pool_data_t       d;
  /* 该值用于判断申请的内存属于大块还是小块 */
  size_t                max;
  /* 当有多个小块内存池形成链表时,current指向分配内存时第一个小块内存池
   * 其实意思就是指向链表中可以用于分配内存的第一个内存池
   * 这样就不用在分配时从头开始遍历了
   */
  ngx_pool_t           *current;

  ngx_chain_t          *chain;
  /* 申请大块的内存直接从堆中分配
   * 由于需要在内存池释放时同时也要释放内存
   * 因此需要管理分配的大块内存
   * 于是就把每次分配的大块内存通过large组成一个单链表
   */
  ngx_pool_large_t     *large;
  /* 将所有需要释放的资源(比如文件等)通过cleanup组成一个单链表 */
  ngx_pool_cleanup_t   *cleanup;
  /* 内存池执行中输出日志的对象 */
  ngx_log_t            *log;
};
```

关于大/小块的区分标准,是由`NGX_MAX_ALLOC_FROM_POOL`宏定义决定的:`#define NGX_MAX_ALLOC_FROM_POOL （ngx_pagesize - 1)`。`ngx_pagesize`是内存中每页的大小,x86下是4KB,即4096(可通过`getpagesize()`获取每页大小)。由此可见,在不同的机器上,小块内存和大块内存之间的界线并不是一个定值。当申请的内存小于`NGX_MAX_ALLOC_FROM_POOL + sizeof(ngx_pool_t)`时,则为小块内存,需要通过`ngx_pool_data_t d`来管理;反之,则在堆中分配,通过`large`管理。<br>

接下来,我们来看看管理小块内存的`ngx_pool_data_t`结构体:<br>

```c++
typedef struct {
  //指向未分配的空闲内存的首地址
  u_char             *last;
  //指向当前小块内存池的末尾
  u_char             *end;
  /* 指向下一个小块内存池
   * 注意next成员的数据类型并不是ngx_pool_data_t,而是ngx_pool_t
   */
  ngx_pool_t         *next;
  /* 当剩余的空间不足以分配出小块内存时,failed加1
   * 当failed大于4后,ngx_pool_t的current指针会指向下一个小块内存池
   */
  ngx_uint_t          failed;
}ngx_pool_data_t;
```

接下来,`ngx_pool_large_t`结构体:<br>

```c++
typedef struct ngx_pool_large_s   ngx_pool_large_t;

struct ngx_pool_large_t {
  //指向下一块大的内存
  ngx_pool_large_t        *next;
  //指向调用ngx_alloc分配的大块内存
  void                    *alloc;
}
```

`ngx_pool_t`中有个成员是`ngx_pool_cleanup_t *cleanup`,用于释放比如文件等非内存的资源,定义如下:<br>

```c++
typedef void (*ngx_pool_cleanup_pt)(void *data);

typedef struct ngx_pool_cleanup_s   ngx_pool_cleanup_t;

struct ngx_pool_cleanup_s {
  //清理的方法,函数指针
  ngx_pool_cleanup_pt     handler;
  //handler指向的方法的参数
  void                   *data;
  //指向下一个ngx_pool_cleanup_t结构体
  ngx_pool_cleanup_t     *next;
};
```

关于内存池相关的结构体就这些,其实通过这些结构体,我们就已经对nginx内存池运作的机制有了一定了解。
接下来我们看看内存池是如何创建、销毁、重置以及分配、释放等。<br>

## 创建内存池

`size`参数代表的大小包括了`ngx_pool_t`结构体的大小,因此若`size`比`ngx_pool_t`还小,可能会导致内存越界(因为会访问ngx_pool_t *p,若没有分配空间,则内存越界)。<br>

```c++
ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{
    ngx_pool_t  *p;

    /* NGX_POOL_ALIGNMENT在core/ngx_palloc.h中宏定义为16
     * ngx_memalign的作用就是将size进行16字节的对齐并分配空间(内部调用memalign对齐并分配空间)
     * malloc/realloc返回的内存地址都是以8字节对齐,如果想要更大粒度的对齐,则可以调用memalign函数
     * 还有一个valloc函数,以页的大小作为对齐长度,内部实现通过memalign实现
     */
    p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);
    if (p == NULL) {
        return NULL;
    }

    /* 初始化管理小块内存池的结构体 */
    //last成员,指向未分配的空闲内存的首地址
    //ngx_pool_t结构体要占一定的空间
    p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    //尾部
    p->d.end = (u_char *) p + size;
    //初始化next指针
    p->d.next = NULL;
    //每次分配失败时会加1,超过4则会将current指针移向下一个小块内存池
    p->d.failed = 0;

    //除去管理内存池的代价之外(ngx_pool_t)真正可用的内存大小
    size = size - sizeof(ngx_pool_t);
    //当size 符合 小块内存的大小标准时, 其值就为size
    //否则值为NGX_MAX_ALLOC_FROM_POOL,证明其是大块内存
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    //指向当前内存池
    p->current = p;
    p->chain = NULL;
    //初始化large、cleanup、log
    p->large = NULL;
    p->cleanup = NULL;
    p->log = log;

    return p;
}
```

在`main`函数的初始化工作中,就调用了`ngx_create_pool`函数创建内存池。接下来是销毁内存池:

## 销毁内存池

```c++
void
ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t          *p, *n;
    ngx_pool_large_t    *l;
    ngx_pool_cleanup_t  *c;

    //遍历cleanup链表,调用handler
    for (c = pool->cleanup; c; c = c->next) {
        if (c->handler) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "run cleanup: %p", c);
            c->handler(c->data);
        }
    }

    //遍历large链表,释放大块内存
    for (l = pool->large; l; l = l->next) {

        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p", l->alloc);

        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }

#if (NGX_DEBUG)

    /*
     * we could allocate the pool->log from this pool
     * so we cannot use this log while free()ing the pool
     */

    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                       "free: %p, unused: %uz", p, p->d.end - p->d.last);

        if (n == NULL) {
            break;
        }
    }

#endif

    //遍历小块内存池形成的链表,逐个释放
    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_free(p);

        if (n == NULL) {
            break;
        }
    }
}
```

释放操作比较简单,没什么好讲的。<br>

## 重置内存池

重置内存池的意思是:将大块内存释放还给操作系统,小块内存不释放而是复用(重置`ngx_pool_data_t`结构体中的`last`成员)。<br>

```c++
void
ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p;
    ngx_pool_large_t  *l;

    //遍历large链表,释放申请的大块内存
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }

    //置空
    pool->large = NULL;

    //遍历小块内存池链表,重置last
    for (p = pool; p; p = p->d.next) {
        p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    }
}
```

## 分配内存

ngxin的内存池提供了4个接口供用户获取内存。分别是:`ngx_palloc`、`ngx_pnalloc`、`ngx_pcalloc`、`ngx_pmemalign`。<br>

前三个函数代码的实现都很类似,只是`ngx_palloc`会分配地址对齐的内存,而`ngx_pnalloc`分配内存时不会进行地址对齐,`ngx_pcalloc`分配地址对齐的内存后再调用`memset`进行清0。<br>

而`ngx_pmemalign`则按照传入的参数`alignment`进行地址对齐分配内存,不过有点比较特殊:这样分配的内存不管申请的size大小有多小,都会在堆中分配然后挂在`large`链表上。<br>

因此下面只分析`ngx_palloc`以及`ngx_pmemalign`这两个函数的源码。<br>
\- ngx_palloc:<br>

源码如下:<br>

```c++
void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    ngx_pool_t  *p;
    //若size <= pool->max,证明是小块内存,直接在小块内存池中分配
    //否则直接在堆中分配
    if (size <= pool->max) {

        //指向目前可用于分配的内存池
        p = pool->current;

        //尝试申请内存
        do {
            /* ngx_align_ptr是一个宏函数,展开如下:
             * #define ngx_align_ptr(p, a)            \
               (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))
             * 这个位运算可能比较难理解
             * ～(a-1)：其中a为2的幂,设右数第n位为非零位,则a-1为右数的n-1位均为1, 则有~(a-1)为最后的n-1位全为0,即此时的值必为a的幂
             * p+(a-1)：这部操作相当于加上a-1为右数的n-1位,比如a为4(0100),则加上(0011)
             * 然后两者相与,得到的值必为a的幂
             */
            m = ngx_align_ptr(p->d.last, NGX_ALIGNMENT);

            //end代表当前小块内存池的尾部
            //若当前内存池的容量还够size大小
            //则进行分配,并返回分配的内存块的起始地址m
            if ((size_t) (p->d.end - m) >= size) {
                p->d.last = m + size;

                return m;
            }
            //否则查看下一个小块内存池
            p = p->d.next;

        } while (p);

        //如果遍历了所有的小块内存池还是没有找到足够的容量
        //那么就分配一个新的小块内存池
        return ngx_palloc_block(pool, size);
    }

    //在堆中分配
    return ngx_palloc_large(pool, size);
}
​```
`ngx_palloc_block`的源码如下:
​```
//只在当前源文件内部使用
static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    size_t       psize;
    ngx_pool_t  *p, *new, *current;

    //获取需要分配的大小(ngx_pool_t加上小块内存池的大小)
    psize = (size_t) (pool->d.end - (u_char *) pool);

    //以NGX_POOL_ALIGNMENT对齐,申请内存
    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);
    if (m == NULL) {
        return NULL;
    }

    //新的ngx_pool_t
    new = (ngx_pool_t *) m;

    //由于该ngx_pool_t只用于小块内存池的管理
    //因此只初始化小块内存池有关的成员
    new->d.end = m + psize;
    new->d.next = NULL;
    new->d.failed = 0;

    //对齐空闲内存的首地址
    //分配大小为size的内存出去
    m += sizeof(ngx_pool_data_t);
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    new->d.last = m + size;

    current = pool->current;

    /* 遍历之前尝试分配大小为size的内存时的小块内存池
     * 将其的failed成员依次加1
     * 若超过4,则将current指向下一个内存池
     */
    for (p = current; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 4) {
            current = p->d.next;
        }
    }

    //将刚分配的ngx_pool_t连接到小块内存池的链表上
    p->d.next = new;

    /* 更新pool->current
     * 若之前小块内存池的failed都大于4了
     * 则将current指向最新分配的那个内存池
     */
    pool->current = current ? current : new;

    return m;
}
`ngx_palloc_large`的源码如下:


static void *
ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void              *p;
    ngx_uint_t         n;
    ngx_pool_large_t  *large;
    //直接调用ngx_alloc分配内存
    p = ngx_alloc(size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    n = 0;
    //将刚分配的大块内存挂到large链表上
    for (large = pool->large; large; large = large->next) {
        if (large->alloc == NULL) {
            large->alloc = p;
            return p;
        }
        //若第4次还是没有找到可以挂上的地方
        //则退出遍历(这是为了防止large链表的节点多了,造成效率的损失)
        if (n++ > 3) {
            break;
        }
    }

    /* 直接申请一个新的ngx_pool_large_t结构体挂在large上 */

    //申请一个ngx_pool_large_t结构体
    large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }

    //将刚分配的大块内存的信息存储在ngx_pool_large_t上
    large->alloc = p;
    //链表的头插法
    large->next = pool->large;
    pool->large = large;

    return p;
}

```

ngx_pmemalign:

源码如下:

```c++
void *
ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment)
{
void *p;
ngx_pool_large_t *large;
  //申请按alignment对齐的内存
  p = ngx_memalign(alignment, size, pool->log);
  if (p == NULL) {
      return NULL;
  }

  //申请一个ngx_pool_large_t的结构体
  //用于存储p的信息(起始地址)
  large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
  if (large == NULL) {
      ngx_free(p);
      return NULL;
  }

  large->alloc = p;
  //挂在large链表上
  large->next = pool->large;
  pool->large = large;

  return p;
}
```

## 释放内存

虽然之前说了随着内存池的销毁,内存池中的内存也会一起销毁。但是对于大块内存来说,可以提前释放的还是应该提前释放,因此nginx提供了`ngx_pfree`函数用于释放大块内存。对应的源码如下:<br>

```c++
ngx_int_t
ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t  *l;

    //遍历large链表,找到p对应的那块内存
    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "free: %p", l->alloc);
            //释放内存
            ngx_free(l->alloc);
            //直接将对应的ngx_pool_large_t结构体中的alloc成员赋为NULL
            //之所以没有选择将该结点撤出链表是为了复用
            //以免下次又要重新对ngx_pool_large_t进行申请
            l->alloc = NULL;

            return NGX_OK;
        }
    }

    return NGX_DECLINED;
}
```

## 释放非内存之外的资源

前面已经说过,nginx实现的内存池也支持管理除了内存之外的资源,比如文件,它们会随着内存池的释放也一起被释放。对应`ngx_pool_t`结构体中管理该功能的成员为`ngx_pool_cleanup_t *cleanup`,之前已经说过,它是一个单链表,每个结点对应着需要释放的资源。<br>
内存池提供了4个接口来管理该部分。分别是`ngx_pool_cleanup_add`、`ngx_pool_run_cleanup_file`、`ngx_pool_cleanup_file`、`ngx_pool_delete_file`。接下来分别分析这几个函数的源码。<br>
\- ngx_pool_cleanup_add<br>

根据该函数名其实都能猜到该函数的作用:添加一个需要在内存池释放时一起释放的资源。<br>

源码如下:<br>

```c++
  ngx_pool_cleanup_t *
  ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
  {
      ngx_pool_cleanup_t  *c;

      //申请ngx_pool_cleanup_t结构体,用于管理
      c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
      if (c == NULL) {
          return NULL;
      }

      //当传入的size不为0时则申请size大小的内存
      //然后赋给data,就可以利用该内存传递参数了
      //否则将data赋为NULL
      if (size) {
          c->data = ngx_palloc(p, size);
          if (c->data == NULL) {
              return NULL;
          }

      } else {
          c->data = NULL;
      }

      //初始化ngx_pool_cleanup_t的其他成员
      c->handler = NULL;
      c->next = p->cleanup;

      p->cleanup = c;

      ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

      return c;
  }

```

\- ngx_pool_run_cleanup_file<br>

该函数的作用对应于`ngx_free`,即提前释放资源。<br>

源码如下:<br>

```c++
  void
  ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd)
  {
      ngx_pool_cleanup_t       *c;
      ngx_pool_cleanup_file_t  *cf;

      //遍历cleanup链表
      for (c = p->cleanup; c; c = c->next) {
          //如果设置的handler函数指针指向的是为释放资源的函数
          if (c->handler == ngx_pool_cleanup_file) {

              cf = c->data;

              //找到该文件,并释放资源
              if (cf->fd == fd) {
                  c->handler(cf);
                  c->handler = NULL;
                  return;
              }
          }
      }
  }
```

ngx_pool_cleanup_file<br>

以关闭文件来释放资源的方法,可设置为handler的值。<br>

源码如下:<br>

```c++
void
ngx_pool_cleanup_file(void *data)
{
ngx_pool_cleanup_file_t *c = data;
  ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d",
                 c->fd);

  //关闭该文件描述符指向的文件
  if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
      ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                    ngx_close_file_n " \"%s\" failed", c->name);
  }
}
```

ngx_pool_delete_file<br>

以删除文件来释放资源的方法,可设置为handler的值。<br>

源码如下:<br>

```c++
void
ngx_pool_delete_file(void *data)
{
ngx_pool_cleanup_file_t *c = data;
  ngx_err_t  err;

  ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d %s",
                 c->fd, c->name);
  //删除文件
  if (ngx_delete_file(c->name) == NGX_FILE_ERROR) {
      err = ngx_errno;

      if (err != NGX_ENOENT) {
          ngx_log_error(NGX_LOG_CRIT, c->log, err,
                        ngx_delete_file_n " \"%s\" failed", c->name);
      }
  }

  if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
      ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                    ngx_close_file_n " \"%s\" failed", c->name);
  }
}
```

#### 小结

关于nginx中的内存池的分析到此结束,由于这部分相对来说比较独立,并且也经常用到,所以放在这里分析。

