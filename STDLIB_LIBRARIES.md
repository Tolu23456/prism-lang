# Prism Standard Library — Complete Reference

This document covers all 34 standard libraries in Prism.

## Core Libraries (28 existing)

### String & Text Processing
- **strings.pr** - String manipulation, padding, formatting, UTF-8
- **regex.pr** - Pattern matching and regular expressions
- **formatting.pr** - Printf-style formatting, templates

### Collections & Iteration
- **iter.pr** - Functional iteration, map, filter, reduce, generators
- **collections.pr** - Arrays, sets, stacks, queues, linked lists
- **vector.pr** - Dynamic arrays with vector operations

### Math & Numbers
- **math.pr** - Trigonometry, logarithms, rounding, constants
- **random.pr** - Pseudorandom number generation
- **complex.pr** - Complex number arithmetic

### Data & Serialization
- **json.pr** - JSON parsing and serialization
- **csv.pr** - CSV reading/writing
- **xml.pr** - XML parsing and serialization

### Date & Time
- **datetime.pr** - Date/time manipulation, timezone support
- **time.pr** - Duration, intervals, formatting

### File & System
- **fs.pr** - File I/O, directory operations
- **path.pr** - Path manipulation and utilities
- **os.pr** - Operating system utilities

### Testing & Debugging
- **testing.pr** - Unit testing framework, assertions
- **debug.pr** - Debugging utilities, error handling

### Security & Hashing
- **crypto.pr** - Encryption, hashing, secure random
- **secrets.pr** - Password generation, secure storage

### Advanced Collections
- **universe.pr** - Universe/set operations
- **universe.pr** - Multiset operations
- **tuple.pr** - Tuple types and operations

### UI & Styling
- **pss.pr** - Prism Style Sheet (CSS-like styling)
- **gui.pr** - GUI components and layouts
- **theme.pr** - Theme management

---

## NEW: 6 Major Library Categories (2279 lines total)

### 1. **datastructures.pr** (420 lines)
Advanced data structure implementations for complex algorithms.

**Key Components:**
- **MinHeap** - Priority queue with O(log n) operations
  - `push(val)` - Add element
  - `pop()` - Remove minimum
  - `peek()` - View minimum

- **MaxHeap** - Reverse priority queue

- **Trie** - Prefix tree for string searching
  - `insert(word)` - Add word
  - `search(word)` - Exact match
  - `startsWith(prefix)` - Prefix search
  - `delete(word)` - Remove word

- **Graph** - Directed/weighted graph operations
  - `addVertex(v)` - Add node
  - `addEdge(from, to, weight)` - Add connection
  - `bfs(start)` - Breadth-first search
  - `dfs(start)` - Depth-first search
  - `dijkstra(start)` - Shortest paths

- **UnionFind** - Disjoint set union with path compression
  - `find(x)` - Find set representative
  - `union(x, y)` - Merge sets
  - `connected(x, y)` - Check connectivity

- **BloomFilter** - Probabilistic set membership
  - `add(item)` - Add element
  - `mightContain(item)` - Membership test (fast, may have false positives)

- **SegmentTree** - Range query data structure
  - `query(l, r)` - Range aggregate operation

- **Stack** - LIFO data structure
- **Queue** - FIFO data structure

**Example Usage:**
```prism
import "lib/datastructures"

let heap = MinHeap()
heap.push(5)
heap.push(3)
heap.push(7)
print(heap.pop())  // 3

let graph = Graph()
graph.addVertex(1)
graph.addVertex(2)
graph.addEdge(1, 2, 5)
let path = graph.dijkstra(1)

let trie = Trie()
trie.insert("hello")
trie.insert("world")
print(trie.search("hello"))  // true
```

---

### 2. **http.pr** (402 lines)
HTTP client, server routing, middleware, and web utilities.

**Key Components:**
- **HTTP Methods & Status Codes**
  - GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS
  - 200 OK, 404 NOT_FOUND, 500 INTERNAL_SERVER_ERROR, etc.

- **MIME_TYPES** - Content type constants
  - json, html, xml, text, csv, pdf, jpeg, png, etc.

- **HttpRequest** - Chainable request builder
  - `setHeader(key, value)` - Add header
  - `setBody(data)` - Set request body
  - `setParam(key, value)` - Add query parameter
  - `asJson()` - Set JSON content type
  - `withAuth(token)` - Add bearer token
  - `build()` - Finalize request

- **HttpResponse** - Response builder
  - `json(data)` - Send JSON
  - `text(text)` - Send plain text
  - `html(html)` - Send HTML
  - `redirect(location)` - 302 redirect
  - `setHeader(key, value)` - Add response header

- **Router** - Route matching and dispatch
  - `get(path, handler)` - GET endpoint
  - `post(path, handler)` - POST endpoint
  - `put(path, handler)` - PUT endpoint
  - `delete(path, handler)` - DELETE endpoint
  - `match(method, path)` - Find handler

- **Middleware** - Request/response pipeline
  - `use(handler)` - Add middleware
  - `execute(req, res)` - Run chain

- **Middleware Helpers**
  - `corsMiddleware(origins)` - CORS support
  - `authMiddleware(secret)` - Authorization
  - `loggingMiddleware()` - Request logging
  - `rateLimitMiddleware(max, window)` - Rate limiting

- **URL Utilities**
  - `parseUrl(url)` - Parse URL and query string
  - `encodeURIComponent(str)` - URL encode
  - `decodeURIComponent(str)` - URL decode

- **Cookie** - Cookie creation and serialization
  - HttpOnly, Secure, SameSite support

**Example Usage:**
```prism
import "lib/http"

let router = Router()
let auth = authMiddleware("secret123")

router.post("/api/users", fn(body) {
    return HttpResponse(STATUS.CREATED, {id: 1, name: "Alice"})
        .asJson()
})

router.get("/files/:id", fn(params) {
    return HttpResponse(STATUS.OK, "File content")
})

let req = HttpRequest(POST, "https://api.example.com/data")
    .asJson()
    .withAuth("token123")
    .setHeader("X-Custom", "value")
let built = req.build()
```

---

### 3. **compression.pr** (343 lines)
Data compression and encoding utilities.

**Key Components:**
- **Base64 Encoding**
  - `base64Encode(data)` - Encode to base64
  - `base64Decode(encoded)` - Decode from base64

- **Hex Encoding**
  - `hexEncode(data)` - Encode to hex string
  - `hexDecode(hex_str)` - Decode from hex

- **URL Encoding**
  - `urlEncode(str)` - URL percent-encoding
  - `urlDecode(str)` - URL percent-decoding

- **Run-Length Encoding (RLE)**
  - `rleEncode(data)` - Simple compression
  - `rleDecode(encoded)` - Simple decompression

- **LZ77 Compression**
  - `lz77Encode(data, window_size)` - Lempel-Ziv compression
  - `lz77Decode(encoded)` - LZ77 decompression

- **Huffman Coding**
  - `huffmanEncode(data)` - Huffman tree generation
  - Supports custom compression implementations

- **Checksums & Validation**
  - `crc32(data)` - CRC32 checksum
  - `md5Simple(data)` - Simple MD5-like hash
  - `validateChecksum(data, checksum, algo)` - Verify integrity

- **Unified API**
  - `compress(data, method)` - Method-agnostic compression
  - `decompress(data, method)` - Method-agnostic decompression
  - Supports: "rle", "lz77", "base64", "hex"

**Example Usage:**
```prism
import "lib/compression"

let data = "Hello, Hello, World!"
let compressed = rleEncode(data)  // "1H1e1l2o1, 2 1H1e1l2o1, 1W1o1r1l1d1!"

let encoded = base64Encode("secret")  // "c2VjcmV0"
let decoded = base64Decode(encoded)   // "secret"

let url_safe = urlEncode("hello world")  // "hello+world"

let checksum = crc32("data")
print(validateChecksum("data", checksum, "crc32"))  // true
```

---

### 4. **concurrency.pr** (462 lines)
Asynchronous patterns, promises, locks, and concurrency primitives.

**Key Components:**
- **Promise** - Async value container
  - States: "pending", "resolved", "rejected"
  - `then(onResolve, onReject)` - Chain operations
  - `catch(onReject)` - Error handling
  - `finally(callback)` - Cleanup

- **Promise Utilities**
  - `promiseAll(promises)` - Wait for all
  - `promiseRace(promises)` - Wait for first

- **Mutex** - Mutual exclusion lock
  - `lock()` - Acquire lock
  - `unlock()` - Release lock
  - `tryLock()` - Non-blocking attempt

- **Semaphore** - Counting semaphore
  - `acquire()` - Request permit
  - `release()` - Return permit
  - `availablePermits()` - Check availability

- **ReadWriteLock** - Multiple readers, exclusive writer
  - `readLock()` - Shared read access
  - `writeLock()` - Exclusive write access
  - `readUnlock()` - Release read lock
  - `writeUnlock()` - Release write lock

- **ThreadPool** - Worker pool for task execution
  - `execute(task)` - Submit task
  - `shutdown()` - Stop accepting tasks
  - `getQueueSize()` - Pending tasks
  - `getActiveCount()` - Running tasks

- **Async Utilities**
  - `timeout(ms)` - Delayed promise
  - `retryAsync(task, attempts, delay)` - Exponential backoff retry
  - `series(tasks)` - Sequential execution
  - `parallel(tasks)` - Concurrent execution

- **EventEmitter** - Pub/sub pattern
  - `on(event, callback)` - Subscribe
  - `off(event, callback)` - Unsubscribe
  - `once(event, callback)` - Single listener
  - `emit(event, data)` - Publish
  - `listenerCount(event)` - Active listeners

**Example Usage:**
```prism
import "lib/concurrency"

let promise = Promise(fn(resolve, reject) {
    sleep(1)
    resolve("done")
})

promise.then(fn(val) {
    print(val)  // "done"
})

let mutex = Mutex()
mutex.lock().then(fn(v) {
    print("Critical section")
    mutex.unlock()
})

let pool = ThreadPool(4)
pool.execute(fn() { return 1 + 1 })

let emitter = EventEmitter()
emitter.on("data", fn(val) { print("Received: " + val) })
emitter.emit("data", 42)
```

---

### 5. **ml.pr** (515 lines)
Machine learning, statistics, and data analysis.

**Key Components:**

- **Descriptive Statistics**
  - `mean(data)` - Average
  - `median(data)` - Middle value
  - `mode(data)` - Most frequent
  - `variance(data)` - Spread squared
  - `stdDev(data)` - Standard deviation
  - `range(data)` - Max - min
  - `quantile(data, q)` - Percentile
  - `iqr(data)` - Interquartile range

- **Correlation & Regression**
  - `covariance(x, y)` - Joint variation
  - `pearsonCorr(x, y)` - Linear correlation (-1 to 1)

- **LinearRegression** - Best-fit line
  - `fit(x, y)` - Train on data
  - `predict(x)` - Make predictions
  - `getParams()` - Access slope and intercept
  - `r_squared` - Fit quality

- **KMeans** - Clustering algorithm
  - `fit(data)` - Train on points
  - `predict(point)` - Assign to nearest cluster
  - `iterations` - Convergence count

- **NaiveBayes** - Probabilistic classifier
  - `fit(X, y)` - Train on labeled data
  - `predict(x)` - Classify new sample

- **Model Evaluation**
  - `accuracy(y_true, y_pred)` - Correct predictions
  - `precision(y_true, y_pred, class)` - True positives / predicted positives
  - `recall(y_true, y_pred, class)` - True positives / actual positives
  - `f1Score(y_true, y_pred, class)` - Harmonic mean
  - `confusionMatrix(y_true, y_pred)` - Classification breakdown

- **Feature Scaling**
  - `standardize(data)` - Zero mean, unit variance (z-score)
  - `normalize(data)` - Scale to [0, 1]

- **Cross Validation**
  - `kFoldSplit(data, k)` - Split into k folds

**Example Usage:**
```prism
import "lib/ml"

let x = [1, 2, 3, 4, 5]
let y = [2, 4, 6, 8, 10]

let model = LinearRegression()
model.fit(x, y)
print(model.predict([6]))  // 12

let kmeans = KMeans(3)
kmeans.fit([[1, 1], [1.5, 1.5], [5, 5], [5.5, 5.5]])
print(kmeans.predict([1.2, 1.2]))  // 0

print(accuracy([0, 1, 1, 0], [0, 1, 0, 0]))  // 0.75
print(stdDev([1, 2, 3, 4, 5]))  // 1.414...
```

---

### 6. **database.pr** (537 lines)
PrismaDB native database engine and ORM patterns.

**Key Components:**

- **PrismaDB** - Embedded database engine (.pdb format)
  - `createTable(name, schema)` - Define table
  - `insert(table, data)` - Add row (returns ID)
  - `select(table, where_fn)` - Query rows
  - `selectOne(table, where_fn)` - Get first match
  - `update(id, updates)` - Modify row
  - `delete(id)` - Remove row
  - `count(table)` - Row count
  - `createIndex(table, column)` - Speed up queries
  - `findByIndex(table, column, value)` - Indexed lookup
  - `backup()` - Full snapshot
  - `export()` - JSON export
  - `import(json)` - JSON restore
  - `transaction(operations)` - ACID operations with rollback

- **QueryBuilder** - SQL-like query construction
  - `select(cols)` - Columns
  - `where(condition)` - Filter
  - `join(table, on)` - INNER JOIN
  - `leftJoin(table, on)` - LEFT JOIN
  - `orderBy(col, dir)` - Sort (ASC/DESC)
  - `groupBy(col)` - Aggregate
  - `limit(n)` - Row limit
  - `offset(n)` - Skip rows
  - `build()` - Get SQL string
  - `execute()` - Run query

- **ConnectionPool** - Resource pooling
  - `acquire()` - Get connection
  - `release(conn)` - Return connection
  - `getAvailableCount()` - Free connections
  - `getActiveCount()` - In-use connections

- **Model** - ORM base class
  - `create(data)` - Insert
  - `findAll()` - Get all
  - `findById(id)` - Get by ID
  - `findBy(col, val)` - Get by column
  - `update(id, data)` - Update
  - `delete(id)` - Delete
  - `count()` - Row count

- **Migration** - Schema versioning
  - `up(fn)` - Define upgrade
  - `down(fn)` - Define downgrade
  - `execute(db)` - Apply migration
  - `rollback(db)` - Undo migration

- **MigrationRunner** - Manage migrations
  - `addMigration(mig)` - Register
  - `runMigrations(db)` - Apply pending
  - `rollbackAll(db)` - Revert all
  - `getStatus()` - Migration state

**PrismaDB File Format (.pdb):**
```
{
  version: "1.0",
  metadata: {
    created: timestamp,
    modified: timestamp,
    record_count: number
  },
  tables: {
    "table_name": {
      schema: {column: {type, required, primary}},
      rows: [{_id, _created, _modified, ...columns}],
      indices: {column: {value: [ids]}}
    }
  }
}
```

**Example Usage:**
```prism
import "lib/database"

let db = PrismaDB("app.pdb")

db.createTable("users", {
    id: {primary: true},
    name: {required: true},
    email: {required: true},
    created: {}
})

let user_id = db.insert("users", {
    id: 1,
    name: "Alice",
    email: "alice@example.com"
})

let users = db.select("users", fn(row) {
    return startsWith(row.email, "alice")
})

db.update(user_id, {name: "Alice Smith"})

let user = db.selectOne("users", fn(row) { return row.id == 1 })

db.createIndex("users", "email")
let by_email = db.findByIndex("users", "email", "alice@example.com")

db.delete(user_id)

print(db.count("users"))  // 0

let backup = db.backup()
let exported = db.export()

let mig1 = Migration("create_users")
    .up(fn(db) {
        db.createTable("posts", {
            id: {primary: true},
            user_id: {},
            title: {required: true}
        })
    })

let runner = MigrationRunner()
runner.addMigration(mig1)
runner.runMigrations(db)

let user_model = Model("users", {})
user_model.setDB(db)
let all_users = user_model.findAll()
```

---

## Library Statistics

| Category | File | Lines | Key Classes |
|----------|------|-------|------------|
| Data Structures | datastructures.pr | 420 | 8 (Heap, Trie, Graph, UnionFind, BloomFilter, SegmentTree, Stack, Queue) |
| HTTP/Web | http.pr | 402 | 7 (Request, Response, Router, Middleware, Cookie) |
| Compression | compression.pr | 343 | 8 (Base64, Hex, RLE, LZ77, Huffman, CRC32) |
| Concurrency | concurrency.pr | 462 | 7 (Promise, Mutex, Semaphore, RWLock, ThreadPool, EventEmitter) |
| ML/Stats | ml.pr | 515 | 5 (LinearRegression, KMeans, NaiveBayes, Statistics) |
| Database | database.pr | 537 | 5 (PrismaDB, QueryBuilder, Model, Migration, ConnectionPool) |
| **TOTAL NEW** | **6 files** | **2,679** | **40+ classes/functions** |

**Grand Total Prism Standard Library:**
- **34 libraries** (28 existing + 6 new)
- **~10,000+ lines** of pure Prism code
- **100+ top-level classes and modules**
- **500+ individual functions and methods**

## Importing Libraries

```prism
import "lib/datastructures"
import "lib/http"
import "lib/compression"
import "lib/concurrency"
import "lib/ml"
import "lib/database"
```

All libraries follow consistent patterns:
- Pure Prism implementation (no external dependencies)
- Chainable/fluent APIs where applicable
- Comprehensive error handling
- Zero-copy where possible
- Full documentation in code

## Performance Notes

- **DataStructures**: O(log n) for heaps, O(1) avg for hash tables
- **Compression**: Speed trades vs. ratio; RLE fastest, LZ77 best compression
- **Concurrency**: Promises are synchronous simulation; full async in runtime
- **ML**: Linear regression is exact; clustering is iterative
- **Database**: In-memory for speed; backup/export for persistence
- **HTTP**: URL parsing, no network I/O in pure implementation
