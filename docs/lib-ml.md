# Machine Learning and Statistics Library Documentation

Statistics, regression, clustering, and classification for Prism.

## Import

```prism
import "lib/ml"
```

## Descriptive Statistics

Basic statistical measures.

```prism
let data = [1, 2, 3, 4, 5]

print(mean(data))        // 3.0
print(median(data))      // 3
print(mode(data))        // null (no repeats)
print(stddev(data))      // 1.414...
print(variance(data))    // 2.0
print(min(data))         // 1
print(max(data))         // 5
print(sum(data))         // 15
```

## Correlation and Covariance

Measure relationships between variables.

```prism
let x = [1, 2, 3, 4, 5]
let y = [2, 4, 5, 4, 6]

print(correlation(x, y))   // 0.835...
print(covariance(x, y))    // 2.0
```

## Linear Regression

Fit a line to data points.

```prism
let regression = LinearRegression()

// Training data
let x = [1, 2, 3, 4, 5]
let y = [2, 4, 5, 4, 6]

regression.fit(x, y)

// Predict
print(regression.predict(3))    // ~4.6
print(regression.predict(6))    // ~6.8

// Evaluate
print(regression.rSquared())    // 0.67...
```

## K-Means Clustering

Partition data into K clusters.

```prism
let kmeans = KMeans(3)  // 3 clusters

let data = [
    [1, 2], [1, 3], [2, 1],     // Cluster 0
    [5, 6], [6, 5], [6, 7],     // Cluster 1
    [10, 10], [10, 11]           // Cluster 2
]

kmeans.fit(data, maxIterations = 10)

for point in data {
    let cluster = kmeans.predict(point)
    print("Point " + str(point) + " -> Cluster " + str(cluster))
}
```

## Naive Bayes Classification

Probabilistic text and feature classification.

```prism
let classifier = NaiveBayes()

// Training
classifier.train([
    {text: "great movie", label: "positive"},
    {text: "amazing film", label: "positive"},
    {text: "terrible movie", label: "negative"},
    {text: "awful film", label: "negative"}
])

// Predict
let prob = classifier.predict("great film")
print(prob)  // {positive: 0.8, negative: 0.2}
```

## Model Evaluation

Assess model performance.

```prism
let yTrue = [1, 0, 1, 1, 0]
let yPred = [1, 0, 1, 0, 0]

print(accuracy(yTrue, yPred))           // 0.8
print(precision(yTrue, yPred))          // 0.666...
print(recall(yTrue, yPred))             // 0.666...
print(f1Score(yTrue, yPred))            // 0.666...

// Confusion matrix
let cm = confusionMatrix(yTrue, yPred)
print(cm)  // {tp: 2, fp: 0, fn: 1, tn: 2}
```

## Feature Scaling

Normalize features to standard ranges.

```prism
let data = [1, 10, 100, 1000]

// Standardization (mean 0, std 1)
let standardized = standardize(data)
print(mean(standardized))    // ~0
print(stddev(standardized))  // ~1

// Normalization (0 to 1)
let normalized = normalize(data)
print(min(normalized))  // 0
print(max(normalized))  // 1
```

## Cross-Validation

Validate model generalization.

```prism
let data = [...]  // training data
let k = 5         // 5-fold validation

let scores = crossValidate(data, k, fn(train, test) {
    let model = LinearRegression()
    model.fit(train.x, train.y)
    return model.rSquared()
})

print(mean(scores))  // Average R-squared across folds
```

## Common Machine Learning Workflows

### Classification Workflow

```prism
import "lib/ml"

// Load data
let data = loadCsv("iris.csv")

// Feature scaling
let X = normalize(data.features)
let y = data.labels

// Split data
let split = trainTestSplit(X, y, 0.2)  // 80% train, 20% test

// Train classifier
let classifier = NaiveBayes()
classifier.train(split.trainX, split.trainY)

// Evaluate
let yPred = []
for x in split.testX {
    push(yPred, classifier.predict(x))
}

print("Accuracy: " + str(accuracy(split.testY, yPred)))
```

### Regression Workflow

```prism
// Load data
let data = loadCsv("house_prices.csv")

// Feature engineering
let X = data.features
let y = data.prices

// Model selection
let models = [
    LinearRegression(),
    PolynomialRegression(2),
    PolynomialRegression(3)
]

// Evaluate each model
for model in models {
    let scores = crossValidate(X, y, 5, model)
    print("Avg R²: " + str(mean(scores)))
}
```

### Clustering Workflow

```prism
// Load data
let data = loadCsv("customers.csv")

// Feature scaling
let X = standardize(data.features)

// Determine optimal clusters
let inertias = []
for k in range(1, 11) {
    let kmeans = KMeans(k)
    kmeans.fit(X)
    push(inertias, kmeans.inertia())
}

// Elbow method shows optimal k
plotInertias(inertias)
```

## Statistical Tests

```prism
let sample1 = [1, 2, 3, 4, 5]
let sample2 = [2, 3, 4, 5, 6]

// Test if means are significantly different
let tStat = tTest(sample1, sample2)
let pValue = 0.05

if tStat < pValue {
    print("Means are significantly different")
}
```
