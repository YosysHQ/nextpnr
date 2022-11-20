use crate::npnr;

pub struct Partition {
    parts: [Option<Box<Partition>>; 4],
    borders: [[Vec<npnr::WireId>; 4]; 4]
}
