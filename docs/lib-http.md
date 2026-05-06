# HTTP Library Documentation

Web server, routing, and middleware framework for Prism.

## Import

```prism
import "lib/http"
```

## Router

HTTP request routing with middleware support.

### Basic Routing

```prism
let router = Router()

router.get("/", fn(req, res) {
    return res.text("Hello, World!")
})

router.post("/api/users", fn(req, res) {
    let body = parseJson(req.body)
    return res.json({id: 1, name: body.name})
})

router.delete("/api/users/:id", fn(req, res) {
    let id = req.params["id"]
    return res.status(204).send()
})
```

### URL Parameters

```prism
router.get("/users/:id", fn(req, res) {
    let userId = req.params["id"]
    let user = database.findUser(userId)
    return res.json(user)
})
```

### Query Parameters

```prism
router.get("/search", fn(req, res) {
    let query = req.query["q"]
    let limit = int(req.query["limit"] ?? "10")
    let results = search(query, limit)
    return res.json(results)
})
```

## Middleware Pipeline

```prism
let router = Router()

// Logging middleware
router.use(fn(req, res, next) {
    print("Request: " + req.method + " " + req.path)
    return next()
})

// Auth middleware
router.use(fn(req, res, next) {
    if not req.headers["Authorization"] {
        return res.status(401).json({error: "Unauthorized"})
    }
    return next()
})

router.get("/protected", fn(req, res) {
    return res.text("Protected resource")
})
```

## Built-in Middleware

### CORS
```prism
router.use(cors({
    origin: "https://example.com",
    methods: ["GET", "POST"],
    credentials: true
}))
```

### Rate Limiting
```prism
router.use(rateLimit({
    windowMs: 15 * 60 * 1000,  // 15 minutes
    maxRequests: 100
}))
```

### JSON Body Parsing
```prism
router.use(jsonParser())

router.post("/api/data", fn(req, res) {
    let data = req.body  // Already parsed JSON
    return res.json({received: data})
})
```

## HttpRequest

```prism
let req = HttpRequest("GET", "https://api.example.com/data")
    .header("Authorization", "Bearer token123")
    .timeout(5000)
    .build()
```

## HttpResponse

```prism
let res = HttpResponse()
    .status(200)
    .header("Content-Type", "application/json")
    .json({success: true})
```

## Cookies

```prism
// Set cookie
res.setCookie("sessionId", "abc123", {
    path: "/",
    secure: true,
    httpOnly: true,
    maxAge: 3600
})

// Get cookie
let sessionId = req.cookies["sessionId"]
```

## URL Utilities

```prism
let url = parseUrl("https://user:pass@example.com:8080/path?query=1")
print(url.host)      // "example.com"
print(url.port)      // "8080"
print(url.pathname)  // "/path"
print(url.search)    // "?query=1"

let encoded = encodeUrl("hello world")  // "hello%20world"
let decoded = decodeUrl("hello%20world")  // "hello world"
```

## HTTP Status Codes

- 200: OK
- 201: Created
- 204: No Content
- 301: Moved Permanently
- 304: Not Modified
- 400: Bad Request
- 401: Unauthorized
- 403: Forbidden
- 404: Not Found
- 500: Internal Server Error

## MIME Types

```prism
print(getMimeType("index.html"))   // "text/html"
print(getMimeType("style.css"))    // "text/css"
print(getMimeType("data.json"))    // "application/json"
print(getMimeType("image.png"))    // "image/png"
```
