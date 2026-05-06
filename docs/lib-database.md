# Database Library Documentation

PrismaDB embedded database engine for Prism.

## Import

```prism
import "lib/database"
```

## Quick Start

```prism
let db = PrismaDB("app.pdb")

// Create table
db.exec("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, email TEXT)")

// Insert
let userId = db.insert("users", {name: "Alice", email: "alice@example.com"})

// Query
let user = db.selectOne("users", {id: userId})

// Update
db.update("users", {id: userId}, {name: "Alice Smith"})

// Delete
db.delete("users", {id: userId})
```

## CRUD Operations

### Create (Insert)

```prism
let id = db.insert("users", {
    name: "Bob",
    email: "bob@example.com",
    age: 30
})
```

### Read (Select)

```prism
// Single record
let user = db.selectOne("users", {id: 1})

// Multiple records
let allUsers = db.select("users")
let adults = db.select("users", {age: {$gte: 18}})

// Count
let count = db.count("users", {age: {$lt: 18}})
```

### Update

```prism
db.update("users", {id: 1}, {age: 31})

// Update multiple
db.updateMany("users", {status: "active"}, {lastLogin: now()})
```

### Delete

```prism
db.delete("users", {id: 1})

// Delete multiple
db.deleteMany("users", {archived: true})
```

## Query Operators

```prism
// Comparison
{age: {$eq: 30}}     // equal
{age: {$ne: 30}}     // not equal
{age: {$gt: 30}}     // greater than
{age: {$gte: 30}}    // greater or equal
{age: {$lt: 30}}     // less than
{age: {$lte: 30}}    // less or equal

// Array operators
{tags: {$in: ["prism", "database"]}}
{tags: {$nin: ["deprecated"]}}

// String operators
{name: {$contains: "Smith"}}
{email: {$startsWith: "alice"}}
```

## Transactions

```prism
db.transaction(fn() {
    db.insert("orders", {userId: 1, total: 100})
    db.update("inventory", {product: "A"}, {qty: qty - 1})
    // Automatically commits on success, rolls back on error
})
```

## Indexing

```prism
// Create index
db.createIndex("users", "email", {unique: true})

// List indexes
let indexes = db.getIndexes("users")

// Drop index
db.dropIndex("users", "email")
```

## ORM Model Pattern

```prism
class User extends Model {
    static table = "users"
    
    static create(name, email) {
        return Model.insert(User.table, {name: name, email: email})
    }
    
    static find(id) {
        return Model.selectOne(User.table, {id: id})
    }
    
    update(changes) {
        return Model.update(User.table, {id: this.id}, changes)
    }
}

let user = User.find(1)
user.update({name: "Updated Name"})
```

## Migrations

```prism
// Create migration file: migrations/001_create_users.pr
func up(db) {
    db.exec("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)")
}

func down(db) {
    db.exec("DROP TABLE users")
}

// Run migrations
db.migrate()
```

## Backup and Export

```prism
// Backup database
db.backup("app.pdb.backup")

// Export to JSON
let json = db.export()

// Import from JSON
db.import(json)
```

## Connection Pooling

```prism
let pool = ConnectionPool({
    database: "app.pdb",
    poolSize: 10,
    maxWaitTime: 30000
})

let connection = pool.acquire()
connection.select("users")
pool.release(connection)
```

## PrismaDB File Format (.pdb)

PrismaDB files are JSON-based:

```json
{
  "version": "1.0",
  "tables": {
    "users": {
      "schema": {
        "id": "INTEGER PRIMARY KEY",
        "name": "TEXT",
        "email": "TEXT"
      },
      "data": [
        {"id": 1, "name": "Alice", "email": "alice@example.com"},
        {"id": 2, "name": "Bob", "email": "bob@example.com"}
      ],
      "indexes": [
        {"field": "email", "unique": true}
      ]
    }
  },
  "metadata": {
    "created": 1234567890,
    "modified": 1234567890,
    "transactions": []
  }
}
```

## Performance Tips

1. **Index frequently queried fields**: Use unique indexes for email/username
2. **Batch operations**: Use transactions for multiple inserts
3. **Connection pooling**: Reuse database connections
4. **Pagination**: Use limit/offset for large result sets
5. **Query optimization**: Filter early, select specific fields
