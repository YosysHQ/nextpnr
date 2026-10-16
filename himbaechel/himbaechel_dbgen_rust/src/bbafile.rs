use std::convert::TryInto;
use std::io::{Result, Write};

pub struct BBAWriter<'a> {
    out: &'a mut dyn Write,
}

impl<'a> BBAWriter<'a> {
    pub fn new(out: &mut dyn Write) -> BBAWriter {
        BBAWriter { out }
    }
    pub fn u8_val(&mut self, val: u8) -> Result<()> {
        writeln!(&mut self.out, "u8 {}", val)?;
        Ok(())
    }
    pub fn u16_val(&mut self, val: u16) -> Result<()> {
        writeln!(&mut self.out, "u16 {}", val)?;
        Ok(())
    }
    pub fn u32_val(&mut self, val: u32) -> Result<()> {
        writeln!(&mut self.out, "u32 {}", val)?;
        Ok(())
    }
    pub fn i8_val(&mut self, val: i8) -> Result<()> {
        writeln!(&mut self.out, "u8 {}", val)?;
        Ok(())
    }
    pub fn i16_val(&mut self, val: i16) -> Result<()> {
        writeln!(&mut self.out, "u16 {}", val)?;
        Ok(())
    }
    pub fn i32_val(&mut self, val: i32) -> Result<()> {
        writeln!(&mut self.out, "u32 {}", val)?;
        Ok(())
    }
    pub fn pre(&mut self, s: &str) -> Result<()> {
        writeln!(&mut self.out, "pre {}", s)?;
        Ok(())
    }
    pub fn post(&mut self, s: &str) -> Result<()> {
        writeln!(&mut self.out, "post {}", s)?;
        Ok(())
    }
    pub fn push(&mut self, s: &str) -> Result<()> {
        writeln!(&mut self.out, "push {}", s)?;
        Ok(())
    }
    pub fn pop(&mut self) -> Result<()> {
        writeln!(&mut self.out, "pop")?;
        Ok(())
    }
    pub fn ref_label(&mut self, s: &str) -> Result<()> {
        writeln!(&mut self.out, "ref {}", s)?;
        Ok(())
    }
    pub fn ref_slice(&mut self, s: &str, len: usize) -> Result<()> {
        writeln!(&mut self.out, "ref {}", s)?;
        self.u32_val(len.try_into().unwrap())?;
        Ok(())
    }
    pub fn null_slice(&mut self) -> Result<()> {
        self.u32_val(0)?;
        self.u32_val(0)?;
        Ok(())
    }
    pub fn str_val(&mut self, s: &str) -> Result<()> {
        writeln!(&mut self.out, "str |{}|", s)?;
        Ok(())
    }
    pub fn label(&mut self, s: &str) -> Result<()> {
        writeln!(&mut self.out, "label {}", s)?;
        Ok(())
    }
}
