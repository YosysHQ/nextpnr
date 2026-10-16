use std::collections::HashMap;

use std::fs::File;
use std::io::*;

use crate::chipdb::BBAStruct;
use crate::bbafile::BBAWriter;

#[derive(Copy, Clone, PartialEq, Eq, Hash)]
pub struct IdString {
    pub index: u32
}

impl IdString {
    pub fn null() -> IdString {
        IdString {index: 0}
    }
}

pub struct StringPool {
    pub strs: HashMap<String, u32>,
    pub known_id_count : u32
}

fn read_lines(filename: &str) -> Result<Lines<BufReader<File>>> {
    let file = File::open(filename)?;
    Ok(BufReader::new(file).lines())
}

impl StringPool {
    pub fn new() -> StringPool {
        StringPool {
            strs : HashMap::from([("".to_string(), 0)]),
            known_id_count: 1
        }
    }
    pub fn read_constids(&mut self, file: &str) -> Result<()> {
        let lines = read_lines(file)?;
        for line in lines.map_while(Result::ok) {
            let trimmed = line.trim();
            if trimmed.len() < 3 || &trimmed[0..2] != "X(" {
                continue;
            }
            let end_pos = trimmed.rfind(')').unwrap();
            let constid = &trimmed[2..end_pos];
            self.id(constid);
        }
        self.known_id_count = self.strs.len() as u32;
        Ok(())
    }
    pub fn id(&mut self, val: &str) -> IdString {
        if let Some(index) = self.strs.get(val) {
            IdString {index: *index}
        } else {
            let index :u32 = self.strs.len().try_into().unwrap();
            self.strs.insert(val.to_string(), index);
            IdString {index: index}
        }

    }
}

impl BBAStruct for StringPool {
    fn serialise_lists(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.label(&format!("{}_strs", context))?;
        let mut strs : Vec<(&String, u32)> = self.strs.iter().map(|(s, i)| (s, *i)).collect();
        strs.sort_by_key(|(_, i)| *i);
        for (s, idx) in strs.iter() {
            if *idx < self.known_id_count {
                continue;
            }
            bba.str_val(s)?;
        }
        Ok(())
    }
    fn serialise(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.u32_val(self.known_id_count)?;
        bba.ref_slice(&format!("{}_strs", context), self.strs.len() - (self.known_id_count as usize))?;
        Ok(())
    }
}