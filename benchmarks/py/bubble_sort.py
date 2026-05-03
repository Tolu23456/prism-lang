nums = list(range(800, 0, -1))
size = len(nums)

for outer in range(size - 1):
    for inner in range(size - outer - 1):
        if nums[inner] > nums[inner + 1]:
            nums[inner], nums[inner + 1] = nums[inner + 1], nums[inner]

print(nums[0])
print(nums[size - 1])
