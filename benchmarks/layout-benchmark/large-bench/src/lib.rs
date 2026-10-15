use flate2::read::GzDecoder;
use flate2::write::GzEncoder;
use flate2::Compression;
use pulldown_cmark::{html, Options, Parser};
use regex::Regex;
use std::io::{Read, Write};

static MARKDOWN_INPUT: &str = r#"
# Heading Level 1
This is a benchmark document testing instruction cache pressure and branch density.
Here is a list of features:
- Feature 1: Markdown parsing with `pulldown-cmark`.
- Feature 2: Regular expression searches over formatted text.
- Feature 3: In-memory Gzip compression and decompression using pure software Deflate.

## Code Block Section
```rust
fn example_code(x: usize) -> usize {
    let mut sum = 0;
    for i in 0..x {
        sum += i * 2;
    }
    sum
}
```

> Blockquotes are also supported, with *emphasis*, **strong text**, and [links](https://webassembly.org).

### Complex Nested Tables & Patterns
| ID | Name | Value | Description |
|---|---|---|---|
| 100 | Alpha | 42.5 | Test entry A with some long text describing the behavior |
| 200 | Beta | 99.9 | Test entry B with complex markdown formatting and tags |
| 300 | Gamma | 12.3 | Test entry C with another varied payload to stress parsing |
"#;

#[no_mangle]
pub extern "C" fn _start() -> i32 {
    let res = run();
    // Return 0 for success (harness expects return 0)
    std::hint::black_box(res);
    0
}

#[no_mangle]
pub extern "C" fn run() -> i32 {
    let mut checksum = 0i32;

    for _iter in 0..15 {
        // 1. Markdown Parsing -> HTML generation
        let mut options = Options::empty();
        options.insert(Options::ENABLE_TABLES);
        options.insert(Options::ENABLE_FOOTNOTES);
        options.insert(Options::ENABLE_STRIKETHROUGH);
        options.insert(Options::ENABLE_TASKLISTS);

        let parser = Parser::new_ext(MARKDOWN_INPUT, options);
        let mut html_output = String::with_capacity(4096);
        html::push_html(&mut html_output, parser);
        checksum ^= html_output.len() as i32;

        // 2. Complex Regex Compilation and Matching
        let re1 = Regex::new(r"([A-Za-z]+)\s*:\s*([^\n\r]+)").unwrap();
        let re2 = Regex::new(r"\|\s*(\d+)\s*\|\s*([A-Za-z]+)\s*\|\s*([\d\.]+)\s*\|").unwrap();
        let re3 = Regex::new(r"(?i)\b(Feature|Benchmark|Supported|Section)\b").unwrap();

        for cap in re1.captures_iter(&html_output) {
            checksum = checksum.wrapping_add(cap.get(1).map_or(0, |m| m.as_str().len() as i32));
        }

        for cap in re2.captures_iter(MARKDOWN_INPUT) {
            let id: i32 = cap[1].parse().unwrap_or(0);
            checksum = checksum.wrapping_add(id);
        }

        let match_count = re3.find_iter(&html_output).count();
        checksum = checksum.wrapping_add(match_count as i32);

        // 3. In-Memory Gzip Compression & Decompression
        let mut encoder = GzEncoder::new(Vec::new(), Compression::default());
        encoder.write_all(html_output.as_bytes()).unwrap();
        let compressed = encoder.finish().unwrap();
        checksum ^= compressed.len() as i32;

        let mut decoder = GzDecoder::new(&compressed[..]);
        let mut decompressed = Vec::new();
        decoder.read_to_end(&mut decompressed).unwrap();
        checksum = checksum.wrapping_add(decompressed.len() as i32);
    }

    checksum
}

