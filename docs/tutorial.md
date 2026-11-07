# sqlite 教程

> 验证环境：`ct v0.20.0`、macOS arm64、系统 SQLite `3.51.0`（Command Line Tools SDK 的
> `sqlite3.h` / `libsqlite3.tbd`）。文中标注「已实测」的**输出**来自同一环境、同一 SQL 序列
> 的实跑。§3 的 demo 与 §5 的 28 个方法在方法式改写后用 `ct build` 重新编译并运行过
> （demo 输出与 `test/demo.expected.out` 逐字一致），§6 的配方是同一批 `sqlite3_*` 调用的
> 等价改写，未逐个重新编译。Linux 与 Windows 没有验证环境。

## 1. 这个包解决什么问题

`sqlite` 让 CT 程序打开数据库、执行 SQL、准备语句、绑定参数、读取结果并干净地关闭。
它是一个**映射包**，不是数据库：SQLite 负责存储与执行，本包只提供声明和小型调用形状适配。

接口是**方法式**的：连接方法挂在 `SqliteDB *` 上，语句方法挂在 `SqliteStmt *` 上。
两者是本包在 shim 里定义的包装结构体（内部各放一个 SQLite 指针），生成的 C 里方法直接展开成
shim 的 `static inline` 转发，没有 vtable、没有接口值、没有间接调用。

**提供的能力**

- `sqlite3_open_v2` / `sqlite3_close_v2`；
- `exec`（普通执行与带回调执行）；
- `prepare_v2` / `step` / `reset` / `finalize` / `clear_bindings`；
- `bind_text` / `bind_int` / `bind_int64` / `bind_double`；
- `column_count` / `column_name` / `column_type` / `column_int` / `column_int64` /
  `column_double` / `column_text`；
- `errcode` / `errmsg`、`changes`、`last_insert_rowid`、`busy_timeout`、`libversion`、`errstr`。

**明确不做的事（边界）**

- 不处理 BLOB（没有 `bind_blob` / `column_blob`）；
- 不暴露 `sqlite3_value`、虚表、session、`serialize` / `deserialize`、`load_extension`；
- 不映射 16 位接口和变参接口（`sqlite3_config`、`sqlite3_mprintf` 等）；
- 不 vendored SQLite：没有 amalgamation，链接系统 `libsqlite3` 并使用系统头文件；
- 不支持 Windows；
- 不做错误码到枚举的翻译：错误码就是原始 `int`，与 `SQLITE_*` 宏比较；
- 不做 RAII：句柄是可拷贝的裸指针，没有 `Drop`，生命周期靠显式 `close()` / `finalize()`；
  这两个调用是幂等的（内部指针置空），但包装壳本身不释放。

## 2. 安装与引入

### 2.1 包元数据

`ct.pkg` 的内容：

```ini
name = sqlite
version = 0.1.0
licence = PublicDomain
ldflags = -lsqlite3
requires = ct >= 0.20.0
```

`requires` 是软约束：旧版 `ct` 只往 stderr 打警告，安装仍然继续。`ldflags` 会被折进
`ct install` 打印的命令。

### 2.2 获取与安装

当前工作树尚未写入 `index/`、也未打 tag，使用本地目录方式：

```sh
ct get ./sqlite          # 或：ct get file:///path/to/ct-pkg/sqlite
ct install
```

实测输出：

```text
$ ct get ./sqlite
vendored 1 package(s)

$ ct install
ct build -I vendor/sqlite --ldflags '-lsqlite3' <unit>.ct -o build
```

发布到索引后也可以走索引安装（`0.1.0` 目前还没有索引行，这条路径未验证）：

```sh
export CT_INDEX=/path/to/ct-pkg/index/index
ct get sqlite@^0.1.0
ct install
```

`ct get` 把包复制到 `vendor/sqlite/` 并写入 `ct.lock`；`ct install` 离线校验锁文件并打印一条
可直接执行的构建命令，它自己不编译。工程 `ct.pkg` 需要：

```ini
dep = sqlite@^0.1.0
```

`<unit>.ct` 是占位符，换成自己的源文件路径。`ct build` 不读取 `ct.pkg`，所以
`--ldflags '-lsqlite3'` 必须显式传入；`-o` 的父目录要先 `mkdir -p build`。

### 2.3 引入路径

| 想要的东西 | import 写法 |
| --- | --- |
| 连接 / 语句 / 绑定 / 列方法，以及 `sqlite::Lib.*` | `import sqlite::Sqlite;` |

只有这一个 `import`：五个 trait 都在 `sqlite/Sqlite.m.ct` 里。句柄类型
`SqliteDB *` / `SqliteStmt *` 由 `Sqlite.shim.h` 声明，`ct build` 会自动把
`sqlite/sqlite` 加进 `-I`，所以消费文件不必手写它们的 typedef。本教程的示例仍
`#include <sqlite3.h>`，是为了直接用 `SQLITE_OK` / `SQLITE_ROW` 等 C 宏和 `SQLITE_OPEN_*`
flags；shim 本身也会带上这个头，去掉也无妨。

### 2.4 目录与头文件约束

- `.m.ct` 单元的 `namespace` 必须等于它所在目录。包根目录叫 `sqlite`，所以映射单元必须放在
  `sqlite/Sqlite.m.ct`，import 写 `import sqlite::Sqlite;`。把 `Sqlite.m.ct` 放在包根会报：

  ```text
  namespace 'sqlite' does not match the directory ''
  ```

- `ct build` 发现映射单元带 shim 头时，会自动把 `vendor/sqlite/sqlite` 加进 `-I`，不需要自己传；
  实跑命令里出现的 `-I 'vendor/sqlite/sqlite'` 就是它加的。

## 3. 最小可运行示例

完整示例在包内 [`test/demo.ct`](../test/demo.ct)，期望输出在
[`test/demo.expected.out`](../test/demo.expected.out)。

工程结构：

```text
sqlite-demo/
├── ct.pkg
├── src/
│   └── main.ct
└── vendor/            # ct get 创建
```

`ct.pkg`：

```ini
name = sqlite-demo
version = 0.1.0
dep = sqlite@^0.1.0
```

`src/main.ct`（完整文件）：

```ct
#include <sqlite3.h>
#include <stdio.h>

import sqlite::Sqlite;

int main(void)
{
    SqliteDB *db = 0;
    SqliteStmt *ins = 0;
    SqliteStmt *sel = 0;
    int rc;
    int rows = 0;

    rc = sqlite::Lib.open(":memory:", &db);
    printf("open=%d\n", rc);
    if (rc != SQLITE_OK) {
        db.close();
        return 1;
    }

    rc = db.exec("CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT)");
    printf("create=%d\n", rc);

    rc = db.prepare("INSERT INTO people(id, name) VALUES(?, ?)", &ins);
    printf("prepare_insert=%d\n", rc);
    ins.bind_int64(1, 1);
    ins.bind_text(2, "alice", -1);
    printf("step_insert=%d\n", ins.step());
    ins.reset();
    ins.bind_int64(1, 2);
    ins.bind_text(2, "bob", -1);
    printf("step_insert=%d\n", ins.step());
    ins.finalize();
    ins = 0;

    rc = db.prepare("SELECT id, name FROM people ORDER BY id", &sel);
    printf("prepare_select=%d\n", rc);
    while (sel.step() == SQLITE_ROW) {
        rows = rows + 1;
        printf("row %d: id=%lld name=%s\n", rows,
               sel.column_int64(0), sel.column_text(1));
    }
    printf("rows=%d\n", rows);
    sel.finalize();
    sel = 0;

    db.close();
    db = 0;
    printf("closed\n");
    return 0;
}
```

构建与运行（**已实测**）：

```sh
ct get ./sqlite
ct install
mkdir -p build
ct build -I vendor/sqlite --ldflags '-lsqlite3' src/main.ct -o build/main
./build/main
```

真实输出：

```text
open=0
create=0
prepare_insert=0
step_insert=101
step_insert=101
prepare_select=0
row 1: id=1 name=alice
row 2: id=2 name=bob
rows=2
closed
```

为什么是这些数字：

- `open` / `create` / `prepare_*` 是 `SQLITE_OK`（0）；
- `step_insert` 是 `SQLITE_DONE`（101）：INSERT 没有结果行，执行到结束；
- `step()` 每读到一行返回 `SQLITE_ROW`（100），读完后返回 `SQLITE_DONE`（101），
  所以循环数到两行；
- `-I 'vendor/sqlite/sqlite'` 由 `ct build` 自动添加；
- `-lsqlite3` 来自 `ct install` 打印的命令；`ct build` 本身不读 `ct.pkg`。

## 4. 核心概念

### 4.1 两层结构

- **A 层** `sqlite/Sqlite.m.ct`：五个 `pub trait`（`Conn` / `Statement` / `Binding` / `Column` /
  `Lib`）。其中 `Conn` 的宿主是 `SqliteDB *`，`Statement` / `Binding` / `Column` 的宿主都是
  `SqliteStmt *`，`Lib` 是命名空间宿主（没有接收者的东西，外加回调式的 `exec_cb`）。
  每个方法一条 recipe，调用点在生成的 C 里直接展开成 shim 的 `static inline` 调用。
  没有包装符号、没有 vtable、没有接口值。
- **B 层** `sqlite/Sqlite.shim.h`：句柄包装结构体（`struct ct_sqlite_db { sqlite3 *db; }`、
  `struct ct_sqlite_stmt { sqlite3_stmt *st; }`，CT 侧别名为 `SqliteDB` / `SqliteStmt`）、
  幂等的 `close` / `finalize`、出参、默认 flags 的
  `open`、`sqlite3_column_text` 的 `const unsigned char *` 到 `const char *` 窄化，以及全部
  NULL 安全转发。全部 `static inline`，没有 `.shim.c`。

宿主类型必须与 shim 里的拼写逐字一致：`SqliteDB *` 是带一个 `*` 的指针类型，
`SqliteStmt **` 是二级指针。trait 签名里写错星号个数，recipe 就无法把实参交给 shim 里的
同名函数。

### 4.2 句柄与语句生命周期

```text
Lib.open ──> db.exec / db.prepare ──> st.bind_* / st.step / st.column_*
   │                    │
   │                    └──> st.reset ──> 重新 bind / step
   │                    └──> st.finalize
   └──> db.close（必须在所有语句 finalize 之后）
```

规则：

- 句柄是包装结构体的指针：`SqliteDB *db = 0;`、`SqliteStmt *st = 0;`。包装壳由本包
  `malloc`，内部才是 SQLite 的原始指针；调用方只碰外层。
- **没有 `Drop`**：句柄是可拷贝的普通指针，自动析构会在副本上重复关闭同一个句柄。
  生命周期必须显式管理：`db.close()` / `st.finalize()`。
- 这两个方法是**幂等**的：内部指针置空，第二次调用返回 `SQLITE_OK`；对 0 调用同样返回
  `SQLITE_OK`。所以关闭后不必再写 `db = 0;` / `st = 0;`（写了也无害）。
- 包装壳本身**不释放**：每次 `open` / `prepare` 泄漏一个 8/16 字节的结构体。这是为了二次
  关闭不触碰已释放内存而付的固定代价，与 `std::io` 的 `ct_io_file` 相同。
- **先 finalize 语句，再 close 连接**。`sqlite3_close_v2` 在还有未 finalize 的语句时会推迟
  真正关闭；语句句柄和资源要等 `finalize()` 才释放。
- `open` 失败时 `db` 仍非空（shim 无论成败都回填包装壳，因为 `sqlite3_open_v2` 可能返回一个
  可关闭的句柄），所以失败路径同样调用 `db.close()`。
- `prepare` 失败时 `*out` 得到的是内部指针为 0 的包装壳，之后 `finalize()` / `step()` 安全
  （`finalize` 返回 `SQLITE_OK`，`step` 转发的空语句由 SQLite 处理）。

### 4.3 语句状态机

```text
prepare ──> [未执行] ──step──> SQLITE_ROW ──step──> ... ──step──> SQLITE_DONE
                ^                                                        │
                └──────────────── reset + （可选）clear_bindings <──────┘
```

- `step()` 返回 `SQLITE_ROW`（100）表示有一行结果，可读 `column_*`；
- 返回 `SQLITE_DONE`（101）表示执行完成；
- 其他值是错误码，用 `errmsg()` / `errcode()` 读详情；
- 想再次执行同一条语句：`reset()`，然后重新 `bind_*`（`reset` 不会清空绑定值，要清空再调用
  `clear_bindings()`）；
- `finalize()` 销毁语句，可重复调用（第二次是 no-op）；之后内部指针已置空，再 `step` 只会
  得到 SQLite 的 misuse 码，不要依赖。

### 4.4 文本所有权：谁复制、谁借用

| 方向 | 行为 |
| --- | --- |
| `bind_text` | **复制**（recipe 使用 `SQLITE_TRANSIENT`），按给定的 `n` 字节复制；绑定后调用方缓冲区可以立刻复用 |
| `column_text` | **借用** SQLite 的内存，指针只在下一次 `step` / `reset` / `finalize` 之前有效 |
| `errmsg` / `column_name` / `errstr` / `libversion` | SQLite 持有的静态或连接内文本，不要 `free` |

需要跨行保留字符串时，把 `column_text` 的结果拷进自己的缓冲区再 `step`。

## 5. API 逐组讲解

### 5.1 连接：`Conn`（宿主 `SqliteDB *`）

`import sqlite::Sqlite;` 后直接按方法调用，例如 `db.exec(sql)`。

| 方法 | 签名 | 语义与返回值 |
| --- | --- | --- |
| `close` | `int close()` | `sqlite3_close_v2`；返回 `SQLITE_OK` 或错误码；**幂等**，对 0 或已关闭句柄返回 `SQLITE_OK` |
| `errmsg` | `const char *errmsg()` | 最近一次错误的文本；由 SQLite 持有，不要 `free` |
| `errcode` | `int errcode()` | 最近一次错误的原始主错误码 |
| `busy_timeout` | `int busy_timeout(int ms)` | 设置忙等待超时（毫秒）；返回 `SQLITE_OK` 或错误码 |
| `exec` | `int exec(const char *sql)` | 执行一条或多条 SQL，不接收结果行；返回首个错误码，成功为 `SQLITE_OK` |
| `changes` | `int changes()` | 最近一条 INSERT / UPDATE / DELETE 影响的行数 |
| `last_insert_rowid` | `long long last_insert_rowid()` | 最近插入的 rowid |
| `prepare` | `int prepare(const char *sql, SqliteStmt **out)` | 编译 SQL 的第一个语句（忽略 `tail`）；返回错误码，`*out` 写语句句柄（失败时内部指针为 0） |

说明：

- `exec` 会忽略查询结果；要读结果用 `prepare` + `step` + `column_*`，或用
  `sqlite::Lib.exec_cb`（见 §5.5）。
- `changes` 与 `last_insert_rowid` 是连接级状态，容易被后续语句覆盖，用完立刻取值。
- `prepare` 的 `out` 是二级指针：`SqliteStmt *st = 0; db.prepare(sql, &st);`。

### 5.2 语句：`Statement`（宿主 `SqliteStmt *`）

| 方法 | 签名 | 语义与返回值 |
| --- | --- | --- |
| `step` | `int step()` | 前进一行：`SQLITE_ROW` / `SQLITE_DONE` / 错误码 |
| `finalize` | `int finalize()` | 销毁语句；返回最后执行错误或 `SQLITE_OK`；**幂等**，对 0 或已 finalize 句柄返回 `SQLITE_OK` |
| `reset` | `int reset()` | 回到未执行状态，保留绑定；返回之前的执行错误码或 `SQLITE_OK` |
| `clear_bindings` | `int clear_bindings()` | 清空所有参数绑定；返回 `SQLITE_OK` 或错误码 |

说明：

- `prepare` 只编译单条语句：SQL 里即使有第二条语句也不会执行。需要多条就用 `exec` 或分别
  `prepare`。
- `reset` 不清空绑定；参数复用前按需要 `clear_bindings()`。
- `finalize()` 是语句资源释放的唯一入口；重复调用安全，内部指针已置空。

### 5.3 绑定：`Binding`（宿主 `SqliteStmt *`）

参数下标从 **1** 开始。越界时返回 `SQLITE_RANGE`。

| 方法 | 签名 | 语义 |
| --- | --- | --- |
| `bind_text` | `int bind_text(int idx, const char *s, int n)` | 绑定 `n` 字节文本并**复制**（`SQLITE_TRANSIENT`）；`n = -1` 表示到 NUL 为止 |
| `bind_int` | `int bind_int(int idx, int v)` | 绑定 32 位整数 |
| `bind_int64` | `int bind_int64(int idx, long long v)` | 绑定 64 位整数 |
| `bind_double` | `int bind_double(int idx, double v)` | 绑定 double |

`bind_text` 走 `sqlite3_bind_text(..., n, SQLITE_TRANSIENT)`。显式长度让两种旧接口能做的事
仍然能做：

- 已知长度的大字段：传 `n`，避免 SQLite 再扫一遍找 NUL；
- 含 `\0` 的二进制文本：传精确字节数，`n` 之后的字节不会被读；
- 普通 C 字符串：传 `-1`（§3 的 demo 就是这么写的）。

例：`st.bind_text(2, "abcdef", 3)` 写入的是 `abc`。绑定只影响“下一次 step”；`step` 之后要
重新绑定（或依赖 `reset` 保留的旧值）。

### 5.4 结果列：`Column`（宿主 `SqliteStmt *`）

| 方法 | 签名 | 语义 |
| --- | --- | --- |
| `column_count` | `int column_count()` | 结果列数 |
| `column_name` | `const char *column_name(int i)` | 列名；SQLite 持有 |
| `column_type` | `int column_type(int i)` | 列类型码：`SQLITE_INTEGER`(1) / `SQLITE_FLOAT`(2) / `SQLITE_TEXT`(3) / `SQLITE_BLOB`(4) / `SQLITE_NULL`(5) |
| `column_int` | `int column_int(int i)` | 按 SQLite 规则转成 32 位整数 |
| `column_int64` | `long long column_int64(int i)` | 转成 64 位整数 |
| `column_double` | `double column_double(int i)` | 转成 double |
| `column_text` | `const char *column_text(int i)` | 转成文本；**借用**，下一次 step/reset/finalize 前有效；SQL NULL 时返回 0 |

列类型与读法：

- 读文本前可以先判断 `column_type`，避免把 NULL 当字符串；
- `column_text` 只保证 NUL 结尾，不返回长度；文本内含 NUL 会被截断；
- BLOB 列仍可用 `column_text` 读，但按字节解释，且没有 BLOB 绑定接口；本包不做 BLOB 的原样往返。

### 5.5 无接收者入口：`Lib`（命名空间宿主）

| 方法 | 签名 | 语义 |
| --- | --- | --- |
| `open` | `int open(const char *path, SqliteDB **out)` | 以 `SQLITE_OPEN_READWRITE \| SQLITE_OPEN_CREATE` 打开；返回 `SQLITE_OK` 或错误码 |
| `open_v2` | `int open_v2(const char *path, SqliteDB **out, int flags)` | 用显式 flags 打开；返回同上 |
| `exec_cb` | `int exec_cb(SqliteDB *db, const char *sql, ct_sqlite_cb cb, void *ud)` | 执行 SQL 并对每行调用 `cb(ud, n, vals, cols)`；返回 `SQLITE_OK` 或错误码 |
| `libversion` | `const char *libversion()` | SQLite 版本字符串，例如 `3.51.0`；静态持有 |
| `errstr` | `const char *errstr(int code)` | 错误码的静态英文描述，例如 `SQL logic error`；静态持有 |

`Lib` 是命名空间宿主，调用必须写全限定名：`sqlite::Lib.open(":memory:", &db)`。
打开之前还没有句柄，所以 `open` / `open_v2` 只能留在这里。`out` 会被写入一个新分配的包装壳，
原值直接覆盖：传一个仍持有活动句柄的变量会泄漏上一份（先 `close()` 再打开新库）。

`exec_cb` 是唯一保留函数式写法的入口，也是唯一带句柄参数的 `Lib` 方法。原因：它要传的是
一个 C 函数指针加一个 `void *userdata`，回调不能成为接收者的一部分，而 `userdata` 是 CT 状态
进入 C 回调的唯一通道。把这对东西留在一次显式的命名空间调用里，比伪装成连接的方法更能
说明这条 C 边界。回调签名是：

```ct
int on_row(void *ud, int n, char **vals, char **cols)
```

`n` 是列数，`cols[i]` 是列名，`vals[i]` 是值（SQL NULL 时为 0）。回调返回非 0 会中止执行，
`exec_cb` 返回 `SQLITE_ABORT`。调用形如：

```ct
sqlite::Lib.exec_cb(db, "SELECT id, name FROM t", on_row, &count);
```

## 6. 常见任务配方

### 6.1 文件库 + 参数化查询并遍历结果

**已实测**：建表、循环插入 3 行、用 `WHERE id > ?` 查询并遍历。

```ct
#include <sqlite3.h>
#include <stdio.h>

import sqlite::Sqlite;

int main(void)
{
    SqliteDB *db = 0;
    SqliteStmt *ins = 0;
    SqliteStmt *sel = 0;
    int rc;
    int i;

    rc = sqlite::Lib.open("demo.db", &db);
    printf("open=%d\n", rc);
    if (rc != SQLITE_OK) {
        printf("errmsg=%s\n", db.errmsg());
        db.close();
        return 1;
    }

    db.exec("DROP TABLE IF EXISTS notes");
    db.exec("CREATE TABLE notes(id INTEGER PRIMARY KEY, body TEXT)");

    rc = db.prepare("INSERT INTO notes(id, body) VALUES(?, ?)", &ins);
    printf("prepare=%d\n", rc);
    for (i = 1; i <= 3; i++) {
        ins.bind_int(1, i);
        ins.bind_text(2, "note", -1);
        printf("step=%d\n", ins.step());
        ins.reset();
        ins.clear_bindings();
    }
    ins.finalize();
    ins = 0;

    rc = db.prepare("SELECT id, body FROM notes WHERE id > ? ORDER BY id", &sel);
    printf("select_prepare=%d\n", rc);
    sel.bind_int(1, 1);
    while (sel.step() == SQLITE_ROW) {
        printf("id=%d body=%s\n", sel.column_int(0), sel.column_text(1));
    }
    sel.finalize();
    sel = 0;

    printf("changes=%d\n", db.changes());
    db.close();
    printf("closed\n");
    return 0;
}
```

实测输出：

```text
open=0
prepare=0
step=101
step=101
step=101
select_prepare=0
id=2 body=note
id=3 body=note
changes=1
closed
```

要点：

- 同一个语句句柄通过 `reset()` 复用；每次重新绑定前先 `clear_bindings()`，避免上一轮的参数残留；
- `step()` 返回 `SQLITE_DONE` 后 `reset()` 才能再执行；
- `changes()` 返回 1 是因为最后一条写语句是单行 INSERT（查询不会更新它）。

### 6.2 插入并读取 `last_insert_rowid` / `changes`

```ct
#include <sqlite3.h>
#include <stdio.h>

import sqlite::Sqlite;

int main(void)
{
    SqliteDB *db = 0;
    SqliteStmt *st = 0;
    int rc;

    sqlite::Lib.open(":memory:", &db);
    db.exec("CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT)");

    rc = db.prepare("INSERT INTO people(name) VALUES(?)", &st);
    st.bind_text(1, "alice", -1);
    printf("step=%d\n", st.step());
    printf("rowid=%lld changes=%d\n", db.last_insert_rowid(), db.changes());
    st.finalize();
    st = 0;

    rc = db.exec("UPDATE people SET name = 'ALICE' WHERE id = 1");
    printf("update=%d changes=%d\n", rc, db.changes());

    db.close();
    return 0;
}
```

预期输出（**已实测**）：

```text
step=101
rowid=1 changes=1
update=0 changes=1
```

### 6.3 事务与回滚

`exec` 可以执行 `BEGIN` / `COMMIT` / `ROLLBACK`。**已实测**：第二条 INSERT 触发 UNIQUE
冲突后回滚，表里最后是 0 行。

```ct
#include <sqlite3.h>
#include <stdio.h>

import sqlite::Sqlite;

int main(void)
{
    SqliteDB *db = 0;
    SqliteStmt *sel = 0;
    int rc;

    rc = sqlite::Lib.open("tx.db", &db);
    if (rc != SQLITE_OK) {
        printf("open=%d errmsg=%s\n", rc, db.errmsg());
        db.close();
        return 1;
    }
    db.exec("DROP TABLE IF EXISTS acct");
    db.exec("CREATE TABLE acct(id INTEGER PRIMARY KEY, balance INTEGER)");

    printf("begin=%d\n", db.exec("BEGIN"));
    rc = db.exec("INSERT INTO acct(id, balance) VALUES(1, 100)");
    if (rc != SQLITE_OK) {
        printf("insert=%d errmsg=%s\n", rc, db.errmsg());
        db.exec("ROLLBACK");
        db.close();
        return 1;
    }

    rc = db.exec("INSERT INTO acct(id, balance) VALUES(1, 999)");
    printf("conflict=%d errmsg=%s\n", rc, db.errmsg());
    if (rc != SQLITE_OK) {
        printf("rollback=%d\n", db.exec("ROLLBACK"));
    }

    db.prepare("SELECT count(*) FROM acct", &sel);
    if (sel.step() == SQLITE_ROW) {
        printf("count_after=%d\n", sel.column_int(0));
    }
    sel.finalize();
    sel = 0;

    db.close();
    return 0;
}
```

实测输出：

```text
begin=0
conflict=19 errmsg=UNIQUE constraint failed: acct.id
rollback=0
count_after=0
```

要点：冲突时 `errmsg` 是连接级状态，`ROLLBACK` 之后仍能读到；`SQLITE_CONSTRAINT` 是 19。

### 6.4 `exec_cb`：用回调遍历结果

适合一次性脚本：不用自己 prepare / step。**已实测**。

```ct
#include <sqlite3.h>
#include <stdio.h>

import sqlite::Sqlite;

int on_row(void *ud, int n, char **vals, char **cols)
{
    int i;
    int *count;

    count = (int *)ud;
    *count = *count + 1;
    for (i = 0; i < n; i++) {
        printf("%s=%s", cols[i], vals[i] ? vals[i] : "NULL");
        if (i + 1 < n) printf(" ");
    }
    printf("\n");
    return 0;
}

int main(void)
{
    SqliteDB *db = 0;
    int count = 0;
    int rc;

    rc = sqlite::Lib.open(":memory:", &db);
    if (rc != SQLITE_OK) {
        printf("open=%d errmsg=%s\n", rc, db.errmsg());
        db.close();
        return 1;
    }
    db.exec("CREATE TABLE t(a INTEGER, b TEXT)");
    db.exec("INSERT INTO t VALUES(1, 'x'), (2, 'y')");

    rc = sqlite::Lib.exec_cb(db, "SELECT a, b FROM t ORDER BY a", on_row, &count);
    printf("exec_cb=%d rows=%d\n", rc, count);

    db.close();
    return 0;
}
```

实测输出：

```text
a=1 b=x
a=2 b=y
exec_cb=0 rows=2
```

要点：

- 回调返回非 0 会中止执行，`exec_cb` 返回 `SQLITE_ABORT`；
- `vals[i]` 为 SQL NULL 时是 0（这里用 `?:` 显示成 `NULL`）；
- 回调里的字符串只在回调期间有效，要保留就自己复制。

### 6.5 错误处理与 NULL / 类型判断

**已实测**：只读打开不存在的文件得到 14；查询列类型码 1/5/3。

```ct
#include <sqlite3.h>
#include <stdio.h>

import sqlite::Sqlite;

int main(void)
{
    SqliteDB *db = 0;
    SqliteStmt *st = 0;
    int rc;
    int i;

    rc = sqlite::Lib.open_v2("missing.db", &db, SQLITE_OPEN_READONLY);
    printf("open=%d errcode=%d errmsg=%s\n", rc, db.errcode(), db.errmsg());
    db.close();
    db = 0;
    if (rc != SQLITE_OK) {
        printf("errstr=%s\n", sqlite::Lib.errstr(rc));
    }

    rc = sqlite::Lib.open(":memory:", &db);
    db.prepare("SELECT 1 AS a, NULL AS b, 'txt' AS c", &st);
    rc = st.step();
    printf("step=%d count=%d\n", rc, st.column_count());
    for (i = 0; i < st.column_count(); i++) {
        printf("col %d name=%s type=%d\n",
               i, st.column_name(i), st.column_type(i));
    }
    printf("b_is_null=%d\n", st.column_type(1) == SQLITE_NULL);
    st.finalize();
    st = 0;
    db.close();
    db = 0;
    return 0;
}
```

实测输出：

```text
open=14 errcode=14 errmsg=unable to open database file
errstr=unable to open database file
step=100 count=3
col 0 name=a type=1
col 1 name=b type=5
col 2 name=c type=3
b_is_null=1
```

要点：

- `open_v2` 失败时句柄可能非空，所以照样 `db.close()`；
- 读文本列前先看 `column_type`，避免把 NULL 传给 `printf("%s")`。

## 7. 常见错误与排查

### 7.1 `ct check` 报 `'import' is not supported in compile mode`

```text
src/main.ct:4:1: error: 'import' is not supported in compile mode
```

`ct check` 的已知限制，不是本包问题。任何带 `import` 的消费单位都会这样；验证用 `ct build`。

对包内的 `sqlite/Sqlite.m.ct`，`ct check` 还会误报「type host 只允许出现在 .m.ct 单元」——
它把 `.m.ct` 当成普通单元检查了。这条同样是已知误报，门禁只走 `ct build`。

### 7.2 `ct build` 不带包参数时链接失败

```text
Undefined symbols for architecture arm64:
  "_sqlite3_open", referenced from:
      _main in main-....o
ld: symbol(s) not found for architecture arm64
```

`ct build` 不读 `ct.pkg`；`-lsqlite3` 来自 `ct install` 打印的命令。照抄打印的命令，或显式传
`--ldflags '-lsqlite3'`。

### 7.3 `Conn c = Conn::sqlite;` 报 `a static trait has no interface value`

```text
error: a static trait has no interface value
```

映射 trait 是静态的，没有 `_iface` / `_vtbl`，不能装箱、不能当值传递。按方法调用即可：
`db.exec(sql)`、`st.step()`；没有接收者的入口写全限定名 `sqlite::Lib.*`。

### 7.4 为什么是 `sqlite/sqlite/Sqlite.m.ct` 而不是 `sqlite/sqlite.m.ct`

`.m.ct` 的 `namespace` 必须等于所在目录。包根也叫 `sqlite`，单元只能再下一层放进 `sqlite/`，
所以 import 是 `import sqlite::Sqlite;`。放在根目录会报：

```text
namespace 'sqlite' does not match the directory ''
```

### 7.5 `close()` 之后数据库仍被锁 / `SQLITE_BUSY`

先 finalize 所有语句，再 `db.close()`。`sqlite3_close_v2` 在还有存活语句时会推迟真正关闭，
语句句柄和资源要等 `finalize()` 才释放。正常顺序：

```text
st.finalize() ──> db.close()
```

### 7.6 `column_text` 在下一行之后变成旧文本

`column_text` 借用 SQLite 的内存，指针只在下一次 `step` / `reset` / `finalize` 前有效。要跨行
保留就复制到自己的缓冲区；只有 `bind_text` 会替你复制。

### 7.7 `ct get sqlite` 报 git clone 失败

```text
ctc: git clone of 'https://github.com/ct-pkg/sqlite' failed
```

`0.1.0` 还没有 index 行，`ct get sqlite` 会回退到上游仓库 URL 并失败。用本地形式
（`ct get ./sqlite` 或 `ct get file:///path/to/ct-pkg/sqlite`）；`ct install` 与 `ct verify`
是离线的，用本地锁即可。

### 7.8 `SQLITE_MISUSE` / 在错误生命周期上调用

`close()` / `finalize()` 是幂等的，重复关闭同一个句柄不再是错误。剩下的 misuse 来自**顺序**：

- `finalize()` 之后再 `step()` 或 `reset()`：内部指针已置空，SQLite 对空语句返回 misuse 码
  （21），不要依赖这个值；
- 先 `db.close()` 再 finalize 语句：连接已关闭/推迟关闭，行为不可靠；
- 绑定下标从 0 开始：SQLite 参数下标是 1 起的，越界返回 `SQLITE_RANGE`（25）。

### 7.9 `errmsg()` 返回 `not an error` 或 `out of memory`

- `errcode()` 为 0 时，`errmsg()` 正常就是 `not an error`——这不是失败；
- 在句柄为 0 时调用 `errmsg()`，SQLite 会返回 `out of memory` 之类的通用文本，属于 SQLite
  自身语义，本包没有额外包装。先检查 `db != 0`。

## 8. 限制与注意事项

1. **macOS / system SQLite，已验证。** 教程在 macOS arm64、系统 SQLite `3.51.0` 上跑过。
   Linux（`libsqlite3-dev`）预期可用但**未测试**；Windows 不在范围内。
2. **Nothing is vendored.** 包里没有 amalgamation；头文件和库来自平台，`-lsqlite3` 必需。
   macOS 上漏掉它会在链接期报 `Undefined symbols ... _sqlite3_open`。
3. **`sqlite/` 子目录是必需的。** `.m.ct` 的 `namespace` 必须等于所在目录，所以单元在
   `sqlite/Sqlite.m.ct`，import 写 `import sqlite::Sqlite;`。
4. **没有 `Drop` / RAII。** 句柄是可拷贝的指针；自动析构会在值拷贝后重复关闭同一个句柄。
   生命周期显式管理：`close()` / `finalize()` 幂等（对 0 或已关闭句柄返回 `SQLITE_OK`），
   但包装壳不释放——每次 `open` / `prepare` 留下一个 8/16 字节的结构体。永远先 finalize
   语句，再 close 连接。
5. **方法宿主是本包的包装类型。** `Conn` 挂 `SqliteDB *`，`Statement` / `Binding` /
   `Column` 挂 `SqliteStmt *`；两者由 `Sqlite.shim.h` 声明，`ct build` 自动加 `-I`。
   包装壳里才是 `sqlite3 *` / `sqlite3_stmt *`，所以消费文件不必再为句柄类型
   `#include <sqlite3.h>`（用 `SQLITE_*` 宏时需要）。
6. **变参 API 没有映射。** `sqlite3_config`、`sqlite3_mprintf` 等是 `...` 函数。CT 可以直接
   调用它们（声明按原样发射），但静态 trait 方法不能声明 `...`；要映射只能写固定参数的 shim。
   `exec_cb` 就是这样一个固定参数入口（回调 + `userdata`）。
7. **错误是原始整数。** 包不把错误码翻译成枚举。`#include <sqlite3.h>` 后与 C 宏比较
   （`SQLITE_OK`、`SQLITE_ROW`、`SQLITE_DONE`、`SQLITE_CONSTRAINT` …），用 `errmsg()` 读文本。
8. **文本生命周期因方向而异。** `bind_text` 复制（`SQLITE_TRANSIENT`），缓冲区可立刻复用；
   `column_text` 借用，只在下一次 `step` / `reset` / `finalize` 前有效。
9. **没有 BLOB、value 或扩展。** `bind_blob`、`column_blob`、`sqlite3_value_*`、虚表、session、
   `load_extension` 都没有暴露。`exec_cb` 的 `vals` / `cols` 是 SQLite 原样传入的 `char **`。
10. **`ct check` 不是验证路径。** 任何带 `import` 的消费单位都会报
    `'import' is not supported in compile mode`；用 `ct build`。
11. **线程模型取决于平台构建。** 包从不调用 `sqlite3_config`，也不选择线程模式；macOS 系统
    构建默认是 serialized。多线程共享一个连接 / 语句仍要自己加锁，语句尤其不能跨线程
    同时 `step`。
12. **`prepare` 只取单条语句。** 传入多条 SQL 时只编译第一条（`tail = 0`）；需要多条就分别
    prepare 或用 `exec`。
13. **读文本没有长度，写文本有。** `bind_text` 接受显式 `n`（`-1` 表示到 NUL），所以含 `\0`
    的文本和已知长度的大字段都能绑；`column_text` 仍只保证 NUL 结尾、不返回长度，需要精确
    字节时本包没有对应接口（BLOB 未提供）。
14. **语句与连接的顺序是硬约束。** 先 `finalize()` 再 `close()`；反过来会让
    `sqlite3_close_v2` 推迟关闭，资源留到进程结束。
15. **版本与许可**：包版本 `0.1.0`，当前工作树未打 tag、未写入 `index/`；编译器门槛
    `requires = ct >= 0.20.0`，验证使用 `ct v0.20.0`。链接依赖是平台的 `libsqlite3` 与
    `sqlite3.h`（`ldflags = -lsqlite3`）。SQLite 本身是 public domain；本包的 CT 代码、shim、
    README 与 demo 是 MIT，见 [../LICENSE](../LICENSE)。
