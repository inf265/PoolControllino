# Memory Optimization Opportunities

This document identifies areas where memory can be saved in the PoolControllino project.

## Current Memory Usage Analysis

### 1. **Large Buffers (Biggest Impact)**

#### `switchConfigRaw[1024]` in PoolControlContext.hpp (Line 91)
- **Current**: 1024 bytes
- **Actual usage**: ~615 bytes (from Defaults.json)
- **Savings**: ~400 bytes by reducing to 768 bytes
- **Risk**: Low - current JSON is ~615 bytes, 768 should be safe with margin
- **Action**: Change to `char switchConfigRaw[768]{0};`

#### Multiple 1024-byte buffers
- `char data[1024]` in Networking.hpp (line 79)
- `char memory[1024]` in WebServer.hpp (line 160)
- **Note**: These are stack-allocated, only used during function execution
- **Savings**: Could be reduced, but need to verify actual JSON size
- **Current JSON size**: ~560-600 bytes
- **Action**: Reduce to 768 bytes each = **~512 bytes total savings**

#### `TMPMEM_SIZE = 650` in WebServer.hpp (line 214)
- **Current**: 650 bytes
- **Usage**: POST request body parsing
- **Actual JSON size**: ~615 bytes
- **Savings**: Could reduce to 768 if we reduce switchConfigRaw
- **Action**: Keep as-is for now (safety margin needed)

### 2. **String Objects (Heap Allocation)**

#### `String errorText` and `String warningText` in PoolControlContext.hpp (Lines 33, 36)
- **Current**: Dynamic heap allocation (variable size)
- **Problem**: String objects fragment heap and can cause memory issues
- **Savings**: Replace with fixed-size char arrays
- **Action**: 
  ```cpp
  // Replace:
  String errorText;
  String warningText;
  
  // With:
  char errorText[128]{0};
  char warningText[128]{0};
  ```
- **Savings**: Eliminates heap fragmentation, saves ~50-200 bytes heap overhead
- **Impact**: Need to update all `.c_str()` calls to direct array usage

### 3. **Timestamp Buffers**

#### `errorTimestamp[32]` and `warningTimestamp[32]` in PoolControlContext.hpp
- **Current**: 32 bytes each (64 bytes total)
- **Usage**: "DD.MM.YYYY hh:mm:ss" format = 19 bytes + null = 20 bytes
- **Optimization**: Already optimal (32 bytes provides margin)

### 4. **Other Small Optimizations**

#### `clientIP[16]` in PoolControlContext.hpp
- **Current**: 16 bytes
- **Usage**: IPv4 address = "xxx.xxx.xxx.xxx" = 15 bytes + null = 16 bytes
- **Optimization**: Already optimal

#### `date[20]` buffers (temporary, stack-allocated)
- **Current**: 20 bytes
- **Usage**: "DD.MM.YYYY hh:mm:ss" = 19 bytes + null = 20 bytes
- **Optimization**: Already optimal

## Recommended Optimizations (Priority Order)

### High Priority (Easy, High Impact)

1. **Replace String objects with char arrays** (~50-200 bytes heap savings)
   - File: `PoolControlContext.hpp`
   - Lines: 33, 36
   - **Effort**: Medium (need to update all usage sites)

2. **Reduce `switchConfigRaw` from 1024 to 768 bytes** (~256 bytes)
   - File: `PoolControlContext.hpp`
   - Line: 91
   - **Effort**: Low (change one number, verify it works)

3. **Reduce buffer sizes in Networking.hpp and WebServer.hpp** (~256 bytes each = 512 total)
   - Files: `Networking.hpp` (line 79), `WebServer.hpp` (line 160)
   - **Effort**: Low (change numbers, verify JSON still fits)

### Medium Priority (Requires Testing)

4. **Share buffers where possible** (if functions don't overlap)
   - Could use same buffer for multiple purposes if execution doesn't overlap
   - **Risk**: Medium (need careful analysis of execution flow)
   - **Savings**: Variable

### Low Priority (Marginal Gains)

5. **Optimize DateTime objects** (if they're larger than needed)
   - These are relatively small structures
   - **Savings**: Minimal
   - **Effort**: High (would need to refactor DateTime class)

## Estimated Total Savings

- **Immediate (High Priority)**: ~800-1000 bytes
  - String replacements: 50-200 bytes
  - Buffer reductions: 768 bytes
  - Total: ~818-968 bytes

## Implementation Notes

### String Replacement Considerations

When replacing `String errorText` and `String warningText`:

1. Change declaration in `PoolControlContext.hpp`:
   ```cpp
   char errorText[128]{0};
   char warningText[128]{0};
   ```

2. Update assignments (replace `.c_str()` with direct assignment):
   ```cpp
   // Old:
   ctx->data.errorText = "Some error message";
   
   // New:
   strncpy(ctx->data.errorText, "Some error message", sizeof(ctx->data.errorText) - 1);
   ctx->data.errorText[sizeof(ctx->data.errorText) - 1] = '\0';
   ```

3. Update usage sites:
   - Change `.c_str()` to direct array name
   - Change concatenation to `strncat()` or `snprintf()`

### Buffer Size Reduction

When reducing buffer sizes:

1. Test with actual JSON payloads to ensure they fit
2. Add margin (current usage + 20-30% is safe)
3. Monitor for buffer overflow issues

## Files to Modify

1. `PoolControlContext.hpp` - String replacements, buffer size reduction
2. `Networking.hpp` - Buffer size reduction
3. `WebServer.hpp` - Buffer size reduction
4. All files that use `errorText` and `warningText` - Update String usage

