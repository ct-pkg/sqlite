# 可行性探针记录（sqlite 包，Wave 2 / Phase 2）

> 后续变更：探针里「类型宿主」的方向已成为 0.1.0 的实际 API，但宿主从 `sqlite3 *` /
> `sqlite3_stmt *` 改成了本包的包装类型 `ct_sqlite_db *` / `ct_sqlite_stmt *`（见
> `sqlite/Sqlite.shim.h`）：包装壳让 `close()` / `finalize()` 可以置空内部指针从而幂等，
> 也避免与其他包争抢 `sqlite3 *` 上的方法名。旧的 `Db.h.ct` / `Stmt.h.ct` 函数式层已删除。
> 本文其余内容保留为当时的探针记录。

> 环境：macOS（arm64），`ct v0.19.1`（ct-ast），系统 SQLite `3.51.0`。
> SDK 头：`$(xcrun --show-sdk-path)/usr/include/sqlite3.h`；
> 链接库：`$(xcrun --show-sdk-path)/usr/lib/libsqlite3.tbd`。
> 全部探针源码在 `/tmp/ct-sqlite-probe/`，命令均在 `-std=c99` 下执行。

## 探针 0：系统库与 C 侧基线

```sh
cc -Wall -Wextra /tmp/probe_sqlite_c.c -lsqlite3 -o /tmp/probe_sqlite_c
/tmp/probe_sqlite_c
```

输出：

```text
3.51.0
```

## 探针 1：函数指针作为参数传给 C 函数 —— 通过

先以 `qsort` 验证「函数指针作实参」：

```ct
#include <stdio.h>
#include <stdlib.h>

int by_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

int main(void)
{
    int a[3];
    a[0] = 3;
    a[1] = 1;
    a[2] = 2;
    qsort(a, 3, sizeof(int), by_int);
    printf("%d %d %d\n", a[0], a[1], a[2]);
    return 0;
}
```

```sh
ct build p1_fnptr.ct -o p1_fnptr --ldflags '-lsqlite3'
./p1_fnptr
```

输出：

```text
1 2 3
```

生成物保留了函数指针实参：

```c
qsort(a, 3, sizeof(int), by_int);
```

再以真实 SQLite 回调验证（shim + map，带 `userdata`）：

```c
/* db9.shim.h */
typedef int (*ct_sqlite_cb)(void *ud, int n, char **vals, char **cols);

static inline int ct_db_exec_cb(void *db, char *sql, ct_sqlite_cb cb, void *ud)
{
    return sqlite3_exec((sqlite3 *)db, sql, cb, ud, 0);
}
```

```ct
int on_row(void *ud, int n, char **vals, char **cols)
{
    int *count = ud;
    *count = *count + 1;
    printf("row %d: %s=%s\n", n, cols[0], vals[0]);
    return 0;
}

db9::Db.exec_cb(db, "SELECT a FROM t", on_row, &count);
```

```sh
ct build probe9.ct -I . -o probe9 --ldflags '-lsqlite3'
./probe9
```

输出：

```text
row 1: a=x
count=1
```

生成物：

```c
ct_db_exec_cb((db), ("SELECT a FROM t"), (on_row), ((&count)));
```

结论：函数指针可作参数、可跨单元、可经 shim 传给 `sqlite3_exec`，`userdata` 正常回传。

## 探针 2：`void **` / `sqlite3 **` 出参 —— 通过；recipe 里的强转不支持

CT 直接声明与取址均可用：

```ct
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>

int main(void)
{
    sqlite3 *db = 0;
    void *p = 0;
    void **pp = &p;
    int rc;

    p = malloc(8);
    printf("void** ok: %d\n", *pp != 0);

    rc = sqlite3_open(":memory:", &db);
    printf("open rc=%d db=%p\n", rc, db);
    printf("version=%s\n", sqlite3_libversion());
    sqlite3_close(db);
    free(p);
    return 0;
}
```

```sh
ct build p2_dblptr.ct -o p2_dblptr --ldflags '-lsqlite3'
./p2_dblptr
```

输出（地址随运行变化）：

```text
void** ok: 1
open rc=0 db=0x101420440
version=3.51.0
```

生成物：

```c
sqlite3 * db = 0;
void * * pp = (&p);
rc = sqlite3_open(":memory:", (&db));
```

**失败项（语言缺口）**：`.m.ct` 的 recipe 右侧不能写强转。把
`void **` 直接转成 `sqlite3 **` 的映射报：

```ct
open(path, db, flags) -> sqlite3_open_v2(path, (sqlite3 **)db, flags, 0);
```

```text
./probe8.m.ct:10:77: error: map recipe names 'sqlite3' but it is not a binding parameter
```

绕法（标注为补丁，不是语言能力）：在 `<base>.shim.h` 内做一次 `static inline` 强转：

```c
static inline int ct_sqlite_open_v2(const char *path, void **out, int flags)
{
    return sqlite3_open_v2(path, (sqlite3 **)out, flags, 0);
}
```

```sh
ct build probe8.ct -I . -o probe8 --ldflags '-lsqlite3'
./probe8
```

输出：

```text
rc=21 db=0x100f933f0
errmsg=flags must include SQLITE_OPEN_READONLY or SQLITE_OPEN_READWRITE
close=0
```

（`rc=21` 是因为探针传了 `flags=0`，正好也验证了 `errmsg` 出参路径。）

## 探针 3：变参透传 —— 直接调用通过；trait 方法不支持 `...`

直接以未声明的被调函数发射（与 `printf` 同一条路径），`sqlite3_config` 的两个变参形态都通过：

```ct
printf("plain %s %d\n", "varargs", 7);
rc = sqlite3_config(SQLITE_CONFIG_LOG, log_cb, 0);
```

```sh
ct build p3b_varargs.ct -o p3b --ldflags '-lsqlite3'
./p3b
```

输出：

```text
config rc=0
```

生成物：

```c
rc = sqlite3_config(SQLITE_CONFIG_LOG, log_cb, 0);
```

**失败项（语言缺口）**：静态 trait 方法不能声明 `...`：

```ct
pub trait Db {
    int config(int op, ...);
}
```

```text
./probe6.m.ct:6:24: error: expected a type (tok 4 '...')
```

绕法（标注为补丁）：变参入口不映射成 trait 方法，改成固定参数的 shim，例如
`exec_cb(db, sql, cb, ud)`；`sqlite3_config` 这类若确实需要，也照此提供固定包装。

## 探针 4：不透明句柄按指针保存、跨单元传递 —— 通过

`handle.h.ct` 把 `sqlite3 *` / `sqlite3_stmt *` 存进 `void *`，对外只暴露标量入口：

```ct
pub struct Handle {
    void *db;
}

pub void *handle_open(char *path)
{
    sqlite3 *db = 0;
    struct Handle *h;

    if (sqlite3_open(path, &db) != 0) {
        sqlite3_close(db);
        return 0;
    }
    h = malloc(sizeof(struct Handle));
    h->db = db;
    return h;
}

pub void *handle_stmt(void *raw, char *sql)
{
    struct Handle *h = raw;
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2((sqlite3 *)h->db, sql, -1, &st, 0) != 0) return 0;
    return st;
}
```

消费单元 `import handle;` 后跨单元调用：

```sh
ct build probe4.ct -I . -o probe4 --ldflags '-lsqlite3'
./probe4
```

输出：

```text
stmt=0xa3d417700
col=hello
```

生成物 `#include "handle.h"`，并调用 `handle_open` / `handle_stmt` / `handle_step` / `handle_close`。
结论：不透明句柄以 `void *` 跨单元保存、传递、返回都成立。

## 探针 5：链接 `-lsqlite3`（macOS）—— 通过

```sh
ct build p2_dblptr.ct -o p2_dblptr --ldflags '-lsqlite3'
```

打印的 `cc` 行与结果：

```text
'cc' -std=c99 '.ctbuild/p2_dblptr.c' -lsqlite3 '-o' 'p2_dblptr'
```

去掉 `-lsqlite3` 则明确失败：

```text
Undefined symbols for architecture arm64:
  "_sqlite3_close", referenced from:
      _main in p2_dblptr-42752c.o
  "_sqlite3_libversion", referenced from:
      _main in p2_dblptr-42752c.o
  "_sqlite3_open", referenced from:
      _main in p2_dblptr-42752c.o
ld: symbol(s) not found for architecture arm64
clang: error: linker command failed with exit code 1
```

结论：macOS 上系统 `libsqlite3` 存在，但**必须**显式 `-lsqlite3`（`ct.pkg` 的 `ldflags` 已带）。

## 附加结论（实现时踩到，一并记录）

| 项 | 结果 | 证据 / 说明 |
| --- | --- | --- |
| `.m.ct` 的 `namespace` 必须等于其所在目录 | **硬约束** | 根目录放 `probe7.m.ct`（namespace `probe`）报 `namespace 'probe' does not match the directory ''`；移到 `db7/db7.m.ct` 即通过。因此本包把映射单元放在 `sqlite/Sqlite.m.ct`。 |
| recipe 右侧可用常量实参 | 通过 | `exec(db, sql) -> sqlite3_exec(db, sql, 0, 0, 0)` 展开为 `sqlite3_exec((db), (...), 0, 0, 0)`，`ct build` + 运行通过。 |
| recipe 右侧不可用强转 | 失败（见探针 2） | 强转交给 shim。 |
| 静态 trait 没有接口值 | 硬约束 | `Db d = Db::probe;` 报 `error: a static trait has no interface value`；调用必须写 `sqlite::Conn.exec(db, sql)`。 |
| `long long` / `sqlite3_int64` | 通过 | `long long v = sqlite3_last_insert_rowid(db);` 输出 `1`，`-Wall -Wextra` 零警告。 |
| `const char *` 签名与返回 | 通过 | 探针 `const char *msg(int rc)` 编译并输出 `SQL logic error`，`cc -Wall -Wextra` 零警告。 |
| 类型宿主 `map trait T for type sqlite3 *` | 通过 | `db12` 探针：trait 方法不写接收者，recipe 首参是接收者；`db.errcode()` 展开为 `sqlite3_errcode((db))`。0.1.0 最终把宿主换成了包装类型（见文首「后续变更」），机制相同。 |
| shim 头自动包含 | 通过 | 生成物首行 `#include "Sqlite.shim.h"`，`cc` 行自动带 `-I '<shim 目录>'`。 |
| 生成物零 wrapper | 通过 | `nm -u build` 中 sqlite 相关的未定义符号只有 `_sqlite3_*`（23 个），没有 `ct_*` 符号（shim 全是 `static inline`）。 |
| 跨单元 `Drop` | 未采用 | `Drop` 发射为 `static`，且句柄可被值拷贝，自动关闭会双重释放。历史上的 `db_close` / `stmt_finalize` 是显式幂等的；0.1.0 改为方法式的 `close()` / `finalize()`，幂等由 shim 里的包装结构体保证。 |
