const express = require('express');
const app = express();
const PORT = 5000;

app.get('/', (req, res) => {
  res.send(`<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>prism-lang</title>
  <style>
    body {
      font-family: sans-serif;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      min-height: 100vh;
      margin: 0;
      background: #0f0f1a;
      color: #e0e0ff;
    }
    h1 {
      font-size: 3rem;
      background: linear-gradient(135deg, #a78bfa, #60a5fa);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
      margin-bottom: 0.5rem;
    }
    p {
      color: #9090b0;
      font-size: 1.1rem;
    }
  </style>
</head>
<body>
  <h1>prism-lang</h1>
  <p>A new language is being born. Start building!</p>
</body>
</html>`);
});

app.listen(PORT, '0.0.0.0', () => {
  console.log(`prism-lang dev server running on http://0.0.0.0:${PORT}`);
});
