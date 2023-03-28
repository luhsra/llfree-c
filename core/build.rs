use std::{env, process::Command};

fn main() {
    let root = env::var("CARGO_MANIFEST_DIR").unwrap();
    let c_project_dir = format!("{root}/../llc");

    let is_debug = env::var("PROFILE").unwrap() == "debug";

    // Build the C project
    let output = Command::new("make")
        .arg(format!("DEBUG={}", is_debug as usize))
        .arg("-C")
        .arg(&c_project_dir)
        .arg("build/libllc.a")
        .output()
        .expect("Failed to build C project using Makefile");

    if !output.status.success() {
        panic!(
            "Failed to build C project: {}",
            String::from_utf8_lossy(&output.stderr)
        );
    }

    // Link the C project static library
    println!("cargo:rustc-link-search=native={c_project_dir}/build");

    // Re-run the build script if any C source files change
    println!("cargo:rerun-if-changed={c_project_dir}/Makefile");
    for entry in glob::glob(&format!("{c_project_dir}/*.[hc]")).unwrap() {
        match entry {
            Ok(path) => println!("cargo:rerun-if-changed={}", path.display()),
            Err(e) => println!("Warning: glob error for C source files: {e:?}"),
        }
    }
}
