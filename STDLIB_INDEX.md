# Prism Standard Library — Complete Index

Quick reference for all 34 libraries in the Prism standard library.

## Table of Contents

1. [String & Text Processing](#string--text-processing)
2. [Collections & Iteration](#collections--iteration)
3. [Math & Numbers](#math--numbers)
4. [Data Serialization](#data-serialization)
5. [Date & Time](#date--time)
6. [File & System](#file--system)
7. [Testing & Debugging](#testing--debugging)
8. [Security](#security)
9. [UI & Styling](#ui--styling)
10. [NEW: Advanced Data Structures](#new-advanced-data-structures)
11. [NEW: HTTP & Web](#new-http--web)
12. [NEW: Compression & Encoding](#new-compression--encoding)
13. [NEW: Concurrency](#new-concurrency)
14. [NEW: Machine Learning & Stats](#new-machine-learning--stats)
15. [NEW: Database](#new-database)

---

## String & Text Processing

### **strings.pr**
String manipulation with UTF-8 support
```prism
import "lib/strings"
capitalize("hello")        // "Hello"
padLeft("x", 5, ".")       // "....x"
padCenter("x", 5, ".")     // "..x.."
toLines("a\nb\nc")        // ["a", "b", "c"]
split(",", "a,b,c")        // ["a", "b", "c"]
```

### **regex.pr**
Regular expression engine
```prism
import "lib/regex"
let pattern = regex("\\d+")
pattern.match("123")        // true
pattern.findAll("a1b2c3")   // ["1", "2", "3"]
pattern.replace("a1b2", "X") // "aXbX"
```

### **formatting.pr**
Printf-style string formatting
```prism
import "lib/formatting"
format("%s: %d", "Count", 42)  // "Count: 42"
format("{0} + {1} = {2}", 1, 2, 3)  // "1 + 2 = 3"
```

---

## Collections & Iteration

### **iter.pr**
Functional iteration (map, filter, reduce, generators)
```prism
import "lib/iter"
map([1, 2, 3], fn(x) { return x * 2 })  // [2, 4, 6]
filter([1, 2, 3, 4], fn(x) { return x > 2 })  // [3, 4]
reduce([1, 2, 3], fn(a, b) { return a + b }, 0)  // 6
```

### **collections.pr**
Arrays, stacks, queues, linked lists
```prism
import "lib/collections"
let stack = Stack()
stack.push(1)
stack.pop()  // 1

let queue = Queue()
queue.enqueue("first")
queue.dequeue()  // "first"
```

### **vector.pr**
Dynamic arrays with vector operations
```prism
import "lib/vector"
let v = [1, 2, 3]
dotProduct(v, [4, 5, 6])   // 32
magnitude(v)               // 3.74...
normalize(v)               // [0.267, 0.535, 0.802]
```

---

## Math & Numbers

### **math.pr**
Trigonometry, logarithms, constants
```prism
import "lib/math"
sqrt(16)                   // 4
pow(2, 3)                  // 8
sin(PI / 2)                // 1
log10(100)                 // 2
clamp(50, 0, 100)          // 50
```

### **random.pr**
Pseudorandom number generation
```prism
import "lib/random"
random()                   // [0.0, 1.0)
randInt(1, 100)            // Integer in [1, 100]
randChoice([1, 2, 3])      // Random element
shuffle([1, 2, 3, 4])      // Shuffled array
```

### **complex.pr**
Complex number arithmetic
```prism
import "lib/complex"
let z1 = {re: 3, im: 4}
let z2 = {re: 1, im: 2}
complexAdd(z1, z2)         // {re: 4, im: 6}
complexMul(z1, z2)         // {re: -5, im: 10}
magnitude(z1)              // 5
```

---

## Data Serialization

### **json.pr**
JSON parsing and generation
```prism
import "lib/json"
JSON.parse('{"a": 1}')     // {a: 1}
JSON.stringify({a: 1})     // '{"a": 1}'
JSON.pretty({a: {b: 2}})   // Formatted string
```

### **csv.pr**
CSV reading and writing
```prism
import "lib/csv"
parseCSV("a,b,c\n1,2,3")   // [[a, b, c], [1, 2, 3]]
writeCSV([[1, 2], [3, 4]]) // "1,2\n3,4"
```

### **xml.pr**
XML parsing and serialization
```prism
import "lib/xml"
let doc = parseXML("<root><item>text</item></root>")
getElement(doc, "item")    // {text: "text"}
```

---

## Date & Time

### **datetime.pr**
Date/time manipulation with timezone support
```prism
import "lib/datetime"
let now = now()            // Current timestamp
addDays(now, 7)            // 7 days from now
formatDate(now, "YYYY-MM-DD")  // "2026-05-06"
parseDate("2026-05-06", "YYYY-MM-DD")  // Timestamp
```

### **time.pr**
Duration, intervals, formatting
```prism
import "lib/time"
let duration = {hours: 1, minutes: 30}
toSeconds(duration)        // 5400
formatDuration(duration)   // "1h 30m"
```

---

## File & System

### **fs.pr**
File I/O operations
```prism
import "lib/fs"
readFile("data.txt")       // File contents
writeFile("out.txt", data) // Write to file
exists("file.txt")         // true/false
delete("file.txt")         // Delete file
```

### **path.pr**
Path manipulation utilities
```prism
import "lib/path"
join("dir", "file.txt")    // "dir/file.txt"
dirname("/path/to/file.txt")  // "/path/to"
basename("/path/to/file.txt") // "file.txt"
extension("data.json")     // ".json"
```

### **os.pr**
Operating system utilities
```prism
import "lib/os"
currentDirectory()         // Current working dir
listDirectory(".")         // Files in directory
environment("PATH")        // Environment variable
```

---

## Testing & Debugging

### **testing.pr**
Unit testing framework, assertions
```prism
import "lib/testing"
assert(1 == 1, "should be equal")
assertEqual(a, b)
assertTrue(condition)
assertFalse(condition)
assertThrows(fn() { error("test") })
```

### **debug.pr**
Debugging utilities, error handling
```prism
import "lib/debug"
trace("message")           // Print with context
stackTrace()               // Current call stack
breakpoint()               // Pause execution
```

---

## Security

### **crypto.pr**
Encryption, hashing, secure randomness
```prism
import "lib/crypto"
sha256("password")         // Hash string
hmac("key", "data")        // Message authentication code
randomBytes(32)            // Cryptographic random bytes
encrypt(data, key)         // Encryption
decrypt(cipher, key)       // Decryption
```

### **secrets.pr**
Password generation, secure storage
```prism
import "lib/secrets"
generatePassword(16)       // 16-char random password
hashPassword("password")   // Secure hash with salt
verifyPassword("input", hash)  // Constant-time comparison
tokenize(data)             // Random token generation
```

---

## UI & Styling

### **pss.pr**
Prism Style Sheet (CSS-like styling)
```prism
import "lib/pss"
let styles = parseCSS(".button { color: blue; }")
let computed = applyStyles(element, styles)
```

### **gui.pr**
GUI components and layouts
```prism
import "lib/gui"
let button = Button("Click me")
let layout = VStack([label, input, button])
render(layout)
```

### **theme.pr**
Theme management
```prism
import "lib/theme"
let darkTheme = loadTheme("dark")
applyTheme(darkTheme)
```

---

## NEW: Advanced Data Structures

### **datastructures.pr** — 420 lines, 8 classes
Heaps, tries, graphs, union-find, bloom filters, segment trees

**Components:**
- `MinHeap()` — Priority queue with O(log n) ops
- `MaxHeap()` — Max-oriented priority queue
- `Trie()` — Prefix tree for string searching
- `Graph()` — Directed/weighted graphs with shortest path
- `UnionFind(n)` — Disjoint set with path compression
- `BloomFilter(size, hashes)` — Probabilistic set membership
- `SegmentTree(arr, op)` — Range query data structure
- `Stack()` / `Queue()` — LIFO/FIFO collections

```prism
import "lib/datastructures"

let heap = MinHeap()
heap.push(5)
heap.pop()  // 5

let graph = Graph()
graph.dijkstra("start")  // Shortest paths

let trie = Trie()
trie.insert("hello")
trie.search("hello")  // true
```

📖 **Full docs:** `STDLIB_LIBRARIES.md` → "Data Structures"

---

## NEW: HTTP & Web

### **http.pr** — 402 lines, 7 classes
Routing, middleware, cookies, request/response utilities

**Components:**
- `HttpRequest()` — Chainable request builder
- `HttpResponse()` — Response builder with helpers
- `Router()` — RESTful routing for GET/POST/PUT/DELETE
- `Middleware()` — Request/response pipeline
- `corsMiddleware()` — CORS support
- `authMiddleware()` — Authorization
- `Cookie()` — Cookie creation/management

```prism
import "lib/http"

let router = Router()
router.get("/api/users", fn(req) {
    return HttpResponse(STATUS.OK, users).asJson()
})

router.post("/api/users", fn(req) {
    return HttpResponse(STATUS.CREATED, new_user).asJson()
})

let req = HttpRequest(POST, "https://api.example.com")
    .asJson()
    .withAuth("token123")
    .setParam("key", "value")
```

📖 **Full docs:** `STDLIB_LIBRARIES.md` → "HTTP & Web"

---

## NEW: Compression & Encoding

### **compression.pr** — 343 lines, 8 algorithms
Base64, hex, URL encoding, RLE, LZ77, Huffman, CRC32

**Components:**
- `base64Encode/Decode()` — Base64 encoding
- `hexEncode/Decode()` — Hexadecimal encoding
- `urlEncode/Decode()` — URL percent-encoding
- `rleEncode/Decode()` — Run-length encoding
- `lz77Encode/Decode()` — Lempel-Ziv compression
- `huffmanEncode()` — Huffman tree generation
- `crc32()` — CRC32 checksum
- `compress/decompress()` — Generic interface

```prism
import "lib/compression"

base64Encode("hello")      // "aGVsbG8="
rleEncode("AAABBB")        // "3A3B"
urlEncode("hello world")   // "hello+world"
crc32("data")              // Checksum value
```

📖 **Full docs:** `STDLIB_LIBRARIES.md` → "Compression & Encoding"

---

## NEW: Concurrency

### **concurrency.pr** — 462 lines, 7 classes
Promises, locks, async patterns, thread pool, event emitter

**Components:**
- `Promise()` — Async value container with then/catch
- `promiseAll()` — Wait for multiple promises
- `promiseRace()` — Wait for first promise
- `Mutex()` — Mutual exclusion lock
- `Semaphore()` — Counting semaphore
- `ReadWriteLock()` — Multi-reader/single-writer lock
- `ThreadPool()` — Worker pool for tasks
- `EventEmitter()` — Pub/sub pattern
- `timeout()` — Delayed promise
- `retryAsync()` — Exponential backoff retry

```prism
import "lib/concurrency"

let promise = Promise(fn(resolve, reject) {
    sleep(1)
    resolve("done")
})

promise.then(fn(result) { print(result) })

let emitter = EventEmitter()
emitter.on("message", fn(msg) { print(msg) })
emitter.emit("message", "Hello!")

let mutex = Mutex()
mutex.lock().then(fn(_) {
    // Critical section
    mutex.unlock()
})
```

📖 **Full docs:** `STDLIB_LIBRARIES.md` → "Concurrency"

---

## NEW: Machine Learning & Stats

### **ml.pr** — 515 lines, 5+ classes
Statistics, linear regression, clustering, classification, evaluation

**Components:**
- `mean/median/mode()` — Descriptive statistics
- `variance/stdDev()` — Spread measures
- `pearsonCorr/covariance()` — Correlation
- `LinearRegression` — Best-fit line with R²
- `KMeans()` — Clustering algorithm
- `NaiveBayes()` — Probabilistic classifier
- `accuracy/precision/recall/f1Score()` — Model evaluation
- `standardize/normalize()` — Feature scaling
- `kFoldSplit()` — Cross-validation

```prism
import "lib/ml"

print(mean([1, 2, 3, 4, 5]))  // 3
print(stdDev([1, 2, 3, 4, 5]))  // 1.414

let model = LinearRegression()
model.fit([1, 2, 3], [2, 4, 6])
print(model.predict([4]))  // 8

let kmeans = KMeans(3)
kmeans.fit(data)
print(kmeans.predict(point))  // cluster index

print(accuracy(y_true, y_pred))  // 0.85
```

📖 **Full docs:** `STDLIB_LIBRARIES.md` → "Machine Learning & Stats"

---

## NEW: Database

### **database.pr** — 537 lines, 5+ classes
PrismaDB embedded database, ORM, migrations, transactions

**Components:**
- `PrismaDB()` — In-memory database with persistence
  - CRUD: insert, select, update, delete
  - Indexing, backup, export/import
  - Transactions with rollback
- `QueryBuilder()` — SQL-like query construction
- `Model()` — ORM base class for tables
- `Migration()` — Schema versioning
- `MigrationRunner()` — Manage and run migrations
- `ConnectionPool()` — Resource pooling

```prism
import "lib/database"

let db = PrismaDB("app.pdb")

db.createTable("users", {
    id: {primary: true},
    name: {required: true},
    email: {}
})

let id = db.insert("users", {id: 1, name: "Alice", email: "alice@example.com"})
let users = db.select("users", fn(row) { return row.id == 1 })
db.update(id, {name: "Alice Smith"})
db.delete(id)

let User = Model("users", {})
User.setDB(db)
User.create({name: "Bob"})
User.findAll()

let mig = Migration("001_create_users")
    .up(fn(db) { db.createTable("users", {...}) })
    .down(fn(db) { db.dropTable("users") })

let runner = MigrationRunner()
runner.addMigration(mig)
runner.runMigrations(db)
```

📖 **Full docs:** `STDLIB_LIBRARIES.md` → "Database"

---

## Quick Stats

| Category | Libraries | Lines | Purpose |
|----------|-----------|-------|---------|
| String/Text | 3 | 800 | Text processing |
| Collections | 3 | 1200 | Data structures |
| Math | 3 | 900 | Numerical computing |
| Serialization | 3 | 700 | Data formats |
| Date/Time | 2 | 400 | Temporal data |
| File/System | 3 | 600 | I/O operations |
| Testing | 2 | 300 | Quality assurance |
| Security | 2 | 500 | Cryptography |
| UI/Styling | 3 | 400 | User interfaces |
| **Data Structures** | **1** | **420** | **Advanced algorithms** |
| **HTTP/Web** | **1** | **402** | **Web services** |
| **Compression** | **1** | **343** | **Data compression** |
| **Concurrency** | **1** | **462** | **Async patterns** |
| **ML/Stats** | **1** | **515** | **Data science** |
| **Database** | **1** | **537** | **Persistence** |
| **TOTAL** | **34** | **10,601** | **Complete stdlib** |

---

## Getting Started

### Import a Library
```prism
import "lib/strings"       // String utilities
import "lib/math"          // Math functions
import "lib/http"          // HTTP web services
import "lib/ml"            // Machine learning
import "lib/database"      // Database operations
```

### Common Patterns
```prism
// Build a web API
import "lib/http"
import "lib/database"

let db = PrismaDB("app.pdb")
let router = Router()
router.get("/api/data", fn(_) { 
    return HttpResponse(200, db.select("data")).asJson()
})

// Analyze data
import "lib/ml"
let model = LinearRegression()
model.fit(x_train, y_train)
let r2 = model.r_squared

// Process & compress
import "lib/compression"
let encoded = base64Encode(data)

// Concurrent operations
import "lib/concurrency"
promiseAll([task1, task2]).then(fn(results) { ... })
```

---

## Documentation Files

| File | Size | Content |
|------|------|---------|
| `STDLIB_LIBRARIES.md` | 579 lines | Complete API reference for all 34 libraries |
| `QUICK_START_NEW_LIBS.md` | 600 lines | Practical examples for 6 new libraries |
| `LIBRARY_EXPANSION_SUMMARY.md` | 392 lines | Overview of expansion and additions |
| `STDLIB_INDEX.md` | (this file) | Quick index of all libraries |

---

## Next Steps

1. **Read the docs** → `STDLIB_LIBRARIES.md`
2. **Try examples** → `QUICK_START_NEW_LIBS.md`
3. **Import libraries** → `import "lib/NAME"`
4. **Build projects** → Combine multiple libraries for your use case

Happy coding! 🎉
