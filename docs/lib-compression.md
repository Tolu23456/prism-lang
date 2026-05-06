# Compression and Encoding Library Documentation

Data compression and encoding utilities for Prism.

## Import

```prism
import "lib/compression"
```

## Base64 Encoding

Safe encoding for binary and text data.

```prism
let encoded = base64Encode("Hello, World!")
print(encoded)  // "SGVsbG8sIFdvcmxkIQ=="

let decoded = base64Decode("SGVsbG8sIFdvcmxkIQ==")
print(decoded)  // "Hello, World!"
```

## Hex Encoding

Hexadecimal representation of bytes.

```prism
let hex = toHex("ABC")
print(hex)  // "414243"

let bytes = fromHex("414243")
print(bytes)  // "ABC"
```

## URL Encoding

Safe encoding for URLs and query parameters.

```prism
let encoded = encodeUrl("hello world & stuff")
print(encoded)  // "hello%20world%20%26%20stuff"

let decoded = decodeUrl("hello%20world%20%26%20stuff")
print(decoded)  // "hello world & stuff"
```

## Run-Length Encoding (RLE)

Compress repetitive data.

```prism
let compressed = rleEncode("AAAAAABBBBCCCD")
print(compressed)  // "6A4B3C1D"

let decompressed = rleDecode("6A4B3C1D")
print(decompressed)  // "AAAAAABBBBCCCD"
```

## LZ77 Compression

Lempel-Ziv compression algorithm.

```prism
let data = "the quick brown fox jumps over the lazy dog"
let compressed = lz77Compress(data)
print(len(compressed) < len(data))  // true

let decompressed = lz77Decompress(compressed)
print(decompressed == data)  // true
```

## Huffman Coding

Optimal prefix-free binary code.

```prism
let frequencies = {
    'a': 45,
    'b': 13,
    'c': 12,
    'd': 16,
    'e': 9,
    'f': 5
}

let huffman = buildHuffmanTree(frequencies)
let encoded = huffmanEncode("abbacabad", huffman)
let decoded = huffmanDecode(encoded, huffman)
```

## CRC32 Checksums

Detect data corruption.

```prism
let data = "important data"
let checksum = crc32(data)

// Verify data integrity
let newChecksum = crc32(data)
if checksum == newChecksum {
    print("Data is intact")
}

// Append checksum
let withChecksum = data + ":" + str(checksum)
```

## Compression Comparison

| Algorithm | Ratio | Speed | Use Case |
|-----------|-------|-------|----------|
| RLE | 2-10x | Very fast | Repetitive text/images |
| LZ77 | 3-8x | Fast | General purpose |
| Huffman | 2-4x | Medium | Text, logs |
| Base64 | 1.33x | Very fast | Safe transmission |
| Hex | 2x | Very fast | Debugging, display |

## Examples

### Compress Log File

```prism
let logData = readFile("app.log")
let compressed = lz77Compress(logData)
writeFile("app.log.compressed", compressed)

print("Compression ratio: " + str(len(logData) / len(compressed)))
```

### Safe Data Transmission

```prism
let sensitive = "secret password"
let encoded = base64Encode(sensitive)
sendOverNetwork(encoded)

// Receive and decode
let received = receiveFromNetwork()
let decoded = base64Decode(received)
```

### Validate Data Integrity

```prism
let file = readFile("document.pdf")
let checksum = crc32(file)
let manifest = {file: "document.pdf", crc32: checksum}
saveJson("manifest.json", manifest)

// Later: verify file wasn't corrupted
let file2 = readFile("document.pdf")
let checksum2 = crc32(file2)
if checksum == checksum2 {
    print("File integrity verified")
}
```

### URL-Safe Encoding

```prism
let data = {user: "john@example.com", action: "reset&verify"}
let json = toJson(data)
let encoded = encodeUrl(json)
let url = "https://app.com/callback?data=" + encoded
```
