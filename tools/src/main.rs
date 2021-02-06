use std::fs::File;

fn main() {
    let decoder = png::Decoder::new(File::open("../images/head.png").unwrap());
    let (info, mut reader) = decoder.read_info().unwrap();
    // Allocate the output buffer.
    let mut buf = vec![0; info.buffer_size()];
    // Read the next frame. An APNG might contain multiple frames.
    reader.next_frame(&mut buf).unwrap();

    assert_eq!(info.color_type, png::ColorType::Grayscale);
    assert_eq!(info.bit_depth, png::BitDepth::Eight);

    println!("static const char image[] = {{");

    // Break image apart into 8 pixel rows, record each 8-bit column.
    let w = info.width;
    let h = info.height;
    for y_page in 0..(h + 7)/8 {
        print!("    ");
        for x in 0..w {
            let mut c: u8 = 0;
            for y in 0..8 {
                let y_total = y_page * 8 + y;
                if y_total < h {
                    if buf[(y_total * w + x) as usize] >= 0x80 {
                        c |= 1 << y;
                    }
                }
            }
            print!("0x{:02x}, ", c);
        }
        println!();
    }

    println!("}};");
}
