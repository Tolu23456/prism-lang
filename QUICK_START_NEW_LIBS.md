# Quick Start Guide — New Prism Libraries

Get started with the 6 new major libraries in Prism with practical examples.

---

## 1. Data Structures — Build Complex Algorithms

### Dijkstra's Shortest Path Algorithm

```prism
import "lib/datastructures"

let graph = Graph()

// Build network with cities
graph.addVertex("NYC")
graph.addVertex("Boston")
graph.addVertex("Philly")
graph.addVertex("DC")

// Add weighted edges (distance in miles)
graph.addEdge("NYC", "Boston", 215)
graph.addEdge("NYC", "Philly", 95)
graph.addEdge("Philly", "DC", 140)
graph.addEdge("Boston", "DC", 440)

// Find shortest paths from NYC
let distances = graph.dijkstra("NYC")
print(distances)  // {NYC: 0, Philly: 95, Boston: 215, DC: 235}
```

### Word Dictionary with Trie

```prism
let dictionary = Trie()

dictionary.insert("apple")
dictionary.insert("application")
dictionary.insert("apply")
dictionary.insert("banana")

print(dictionary.search("apple"))        // true
print(dictionary.search("app"))          // false
print(dictionary.startsWith("app"))      // true (matches apple, application, apply)
print(dictionary.startsWith("ban"))      // true (matches banana)

dictionary.delete("apple")
print(dictionary.search("apple"))        // false
print(dictionary.getSize())              // 3
```

### Priority Queue with Heap

```prism
let urgent_tasks = MinHeap()

urgent_tasks.push({priority: 5, task: "Email response"})
urgent_tasks.push({priority: 1, task: "Critical bug fix"})
urgent_tasks.push({priority: 3, task: "Feature request"})

// Process in priority order
while not urgent_tasks.isEmpty() {
    let task = urgent_tasks.pop()
    print("Doing: " + task.task)
}

// Output:
// Doing: Critical bug fix
// Doing: Feature request
// Doing: Email response
```

---

## 2. HTTP/Web — Build Web Services

### REST API with Router

```prism
import "lib/http"

let api = Router()

// GET /api/users/:id
api.get("/api/users/:id", fn(params) {
    let user_id = params.id
    // Fetch from database...
    return HttpResponse(STATUS.OK, {
        id: user_id,
        name: "Alice",
        email: "alice@example.com"
    }).asJson()
})

// POST /api/users
api.post("/api/users", fn(body) {
    // Validate and save user...
    return HttpResponse(STATUS.CREATED, {
        id: 42,
        name: body.name
    }).asJson()
})

// DELETE /api/users/:id
api.delete("/api/users/:id", fn(params) {
    // Delete user...
    return HttpResponse(STATUS.NO_CONTENT, "")
})

// Handle request
let handler = api.match("GET", "/api/users/123")
let response = handler({})
```

### Middleware Pipeline

```prism
let app = Middleware()

// Add middleware in order
app.use(loggingMiddleware())
app.use(corsMiddleware(["https://example.com", "*"]))
app.use(fn(req, res, next) {
    print("Custom middleware running")
    next()
})

// Execute pipeline
let req = {method: "GET", path: "/api/data"}
let res = {status: 200, headers: {}}
let final_res = app.execute(req, res)
```

### Make HTTP Requests

```prism
let request = HttpRequest(POST, "https://api.github.com/repos")
    .asJson()
    .withAuth("token_xyz")
    .setHeader("User-Agent", "Prism-App")
    .setParam("org", "prism-lang")
    .setParam("repo", "stdlib")

let built = request.build()
print(built.url)
// https://api.github.com/repos?org=prism-lang&repo=stdlib
```

---

## 3. Compression — Encode and Compress Data

### Reduce Data Size

```prism
import "lib/compression"

// Simple compression for repetitive data
let log = "AAABBBBBCCCDDEEE"
let compressed = rleEncode(log)  // "3A5B3C2D3E"
let decompressed = rleDecode(compressed)

// Safe encoding for URLs
let sensitive = "user@example.com?key=secret&admin=true"
let encoded = urlEncode(sensitive)
let decoded = urlDecode(encoded)

// Data integrity
let file_data = readFile("large.bin")
let integrity_checksum = crc32(file_data)
// Later, verify: print(validateChecksum(file_data, integrity_checksum, "crc32"))
```

### Base64 for Web

```prism
let image_bytes = readBinaryFile("logo.png")
let base64_encoded = base64Encode(image_bytes)

// Embed in HTML
let html = "<img src='data:image/png;base64," + base64_encoded + "'>"

// Decode back
let original = base64Decode(base64_encoded)
```

---

## 4. Concurrency — Async and Parallel

### Promise-Based Async

```prism
import "lib/concurrency"

let fetch_user = Promise(fn(resolve, reject) {
    sleep(1)  // Simulate API call
    resolve({id: 1, name: "Alice"})
})

let fetch_posts = Promise(fn(resolve, reject) {
    sleep(0.5)
    resolve([
        {id: 101, title: "Hello"},
        {id: 102, title: "World"}
    ])
})

// Wait for both
promiseAll([fetch_user, fetch_posts]).then(fn(results) {
    let user = results[0]
    let posts = results[1]
    print("User: " + user.name + ", Posts: " + len(posts))
})

// Or use .race() for first to complete
promiseRace([fetch_user, fetch_posts]).then(fn(winner) {
    print("Got first result: " + JSON.stringify(winner))
})
```

### Thread-Safe Operations with Mutex

```prism
let shared_resource = {count: 0}
let lock = Mutex()

func increment_safely() {
    lock.lock().then(fn(v) {
        let current = shared_resource.count
        sleep(0.01)  // Simulate work
        shared_resource.count = current + 1
        lock.unlock()
    })
}

// Safe for concurrent access
increment_safely()
increment_safely()
increment_safely()
```

### Event-Driven Architecture

```prism
let bus = EventEmitter()

// Subscribe to events
bus.on("user:created", fn(user) {
    print("New user: " + user.name)
})

bus.on("user:created", fn(user) {
    send_welcome_email(user.email)
})

bus.once("app:startup", fn(config) {
    print("App started with config: " + JSON.stringify(config))
})

// Publish events
bus.emit("user:created", {id: 1, name: "Bob", email: "bob@example.com"})
bus.emit("app:startup", {env: "production"})
```

### Retry with Exponential Backoff

```prism
let api_call = fn() {
    // Return true on success, false on failure
    return random() > 0.3  // 70% success rate
}

retryAsync(api_call, 5, 100).then(fn(result) {
    print("Success after retries!")
}).catch(fn(err) {
    print("Failed after max attempts: " + err)
})
```

---

## 5. Machine Learning — Data Science

### Linear Regression

```prism
import "lib/ml"

// Historical data: hours studied -> test score
let hours = [1, 2, 3, 4, 5, 6, 7]
let scores = [50, 55, 65, 70, 78, 82, 88]

let model = LinearRegression()
model.fit(hours, scores)

// Predict for 8 hours
let predicted = model.predict([8])  // ~92
print("Predicted score for 8 hours: " + predicted)

let params = model.getParams()
print("Slope: " + params.slope + ", Intercept: " + params.intercept)
print("R-squared: " + model.r_squared)  // 0.996
```

### Clustering with K-Means

```prism
// Customer spending data: [annual_spend, purchases_count]
let customers = [
    [500, 10],   // Low-value
    [600, 15],
    [5000, 100], // High-value
    [5500, 120],
    [300, 5],    // Ultra-low
]

let clusters = KMeans(3)
clusters.fit(customers)

for i in range(len(customers)) {
    let cluster = clusters.predict(customers[i])
    print("Customer " + i + " in segment " + cluster)
}

// Output:
// Customer 0 in segment 0 (low-value)
// Customer 1 in segment 0
// Customer 2 in segment 1 (high-value)
// Customer 3 in segment 1
// Customer 4 in segment 2 (ultra-low)
```

### Classification with Naive Bayes

```prism
// Training: spam detection
let emails = [
    [
        {"word:viagra": 1, "word:click": 1, "word:now": 1},  // Spam
        {"word:meeting": 0, "word:urgent": 0, "word:lunch": 0}
    ],
    ["spam", "spam", "ham"]  // Labels
]

let classifier = NaiveBayes()
classifier.fit(emails[0], emails[1])

// Classify new email
let new_email = {"word:viagra": 1, "word:meeting": 0}
let prediction = classifier.predict(new_email)  // "spam"
```

### Statistics for Data Analysis

```prism
let data = [10, 20, 30, 40, 50, 60, 70]

print("Mean: " + mean(data))           // 40
print("Median: " + median(data))       // 40
print("Std Dev: " + stdDev(data))      // 20.82
print("Range: " + range(data))         // 60 (70-10)

// Correlation between two variables
let x = [1, 2, 3, 4, 5]
let y = [2, 4, 5, 4, 6]
print("Correlation: " + pearsonCorr(x, y))  // 0.81

// Normalize data to [0, 1]
let normalized = normalize(data)
print(normalized)  // [0, 0.1667, 0.3333, 0.5, 0.6667, 0.8333, 1]
```

### Model Evaluation

```prism
let y_true = [1, 0, 1, 1, 0, 1, 0, 0]
let y_pred = [1, 0, 1, 0, 0, 1, 0, 1]

print("Accuracy: " + accuracy(y_true, y_pred))  // 0.75
print("Precision: " + precision(y_true, y_pred, 1))  // 0.667
print("Recall: " + recall(y_true, y_pred, 1))  // 0.667
print("F1 Score: " + f1Score(y_true, y_pred, 1))  // 0.667
```

---

## 6. Database — Store and Query Data

### PrismaDB In-Memory Database

```prism
import "lib/database"

let db = PrismaDB("app.pdb")

// Create table with schema
db.createTable("users", {
    id: {primary: true},
    name: {required: true},
    email: {required: true},
    age: {},
    created_at: {}
})

// Insert data
let id1 = db.insert("users", {
    id: 1,
    name: "Alice",
    email: "alice@example.com",
    age: 28,
    created_at: now()
})

db.insert("users", {
    id: 2,
    name: "Bob",
    email: "bob@example.com",
    age: 35,
    created_at: now()
})

// Query all
let all_users = db.select("users")
print("Total users: " + db.count("users"))

// Filter query
let young_users = db.select("users", fn(row) {
    return row.age and row.age < 30
})

// Get single record
let alice = db.selectOne("users", fn(row) {
    return row.name == "Alice"
})

// Update
db.update(id1, {age: 29})

// Index for fast lookups
db.createIndex("users", "email")
let by_email = db.findByIndex("users", "email", "alice@example.com")

// Delete
db.delete(id1)

// Backup
let snapshot = db.backup()

// Export/Import
let json_data = db.export()
// Later: db.import(json_data)
```

### ORM Model Pattern

```prism
let User = Model("users", {
    id: "number",
    name: "string",
    email: "string"
})
User.setDB(db)

// ORM operations
let new_user_id = User.create({name: "Charlie", email: "charlie@example.com"})
let all = User.findAll()
let by_id = User.findById(new_user_id)
let by_email = User.findBy("email", "charlie@example.com")

User.update(new_user_id, {name: "Charles"})
User.delete(new_user_id)
print(User.count())
```

### Migrations

```prism
let migration1 = Migration("001_create_users")
    .up(fn(db) {
        db.createTable("users", {
            id: {primary: true},
            name: {required: true},
            email: {required: true}
        })
    })
    .down(fn(db) {
        db.dropTable("users")
    })

let migration2 = Migration("002_add_age")
    .up(fn(db) {
        // In real systems, would alter table
        // For now: recreate with new schema
    })
    .down(fn(db) {
        // Rollback
    })

let runner = MigrationRunner()
runner.addMigration(migration1)
runner.addMigration(migration2)

runner.runMigrations(db)
print(runner.getStatus())
// {total_migrations: 2, executed: 2, pending: 0}

// Later: runner.rollbackAll(db)
```

### Transactions

```prism
// Transfer money between accounts
let success = db.transaction([
    fn(db) { db.update(from_account_id, {balance: balance - amount}) },
    fn(db) { db.update(to_account_id, {balance: balance + amount}) }
])

if success {
    print("Transfer successful")
} else {
    print("Transfer failed, accounts unchanged")
}
```

---

## Combining Multiple Libraries

### Full-Stack Example: Web API with Data Persistence

```prism
import "lib/http"
import "lib/database"
import "lib/ml"

// Database setup
let db = PrismaDB("api.pdb")
db.createTable("predictions", {
    id: {primary: true},
    input: {required: true},
    output: {required: true},
    timestamp: {}
})

// Initialize ML model
let features = [[1, 2], [2, 4], [3, 6], [4, 8]]
let labels = [3, 6, 9, 12]
let model = LinearRegression()
model.fit(features, labels)

// Create API
let router = Router()

router.post("/api/predict", fn(body) {
    let input = body.input
    let prediction = model.predict(input)
    
    let pred_id = db.insert("predictions", {
        id: db.count("predictions") + 1,
        input: JSON.stringify(input),
        output: prediction,
        timestamp: now()
    })
    
    return HttpResponse(STATUS.CREATED, {
        id: pred_id,
        prediction: prediction
    }).asJson()
})

router.get("/api/predictions", fn(_) {
    let all_preds = db.select("predictions")
    return HttpResponse(STATUS.OK, all_preds).asJson()
})

print("API ready")
```

---

## Performance Tips

1. **Data Structures**: Use Trie for string searching, Heap for priority queues, Graph for networks
2. **Compression**: Use RLE for repetitive data, Base64 for safe transmission
3. **Concurrency**: Use Promises for sequential async, Mutex for shared state
4. **Database**: Create indexes on frequently queried columns, use transactions for consistency
5. **ML**: Normalize/standardize features before training, use k-fold validation

---

## Next Steps

- Read `STDLIB_LIBRARIES.md` for detailed API documentation
- Explore library source code in `lib/` directory
- Check `tests/` for more comprehensive examples
- Contribute improvements or new features!
