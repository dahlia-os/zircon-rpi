pub mod shim;

#[cfg(target_arch = "wasm32")]
mod bindings {
    use wasm_bindgen::prelude::*;

    use {super::shim, json5, std::collections::HashMap};

    macro_rules! format_js_err {
        ($msg:literal $(,)?) => {
            Err(JsValue::from_str($msg))
        };
        ($fmt:expr, $($arg:tt)*) => {
            Err(JsValue::from_str(&format!($fmt, $($arg)*).as_str()))
        };
    }

    macro_rules! bail_js_err {
        ($msg:literal $(,)?) => {
            return format_js_err!($msg);
        };
        ($fmt:expr, $($arg:tt)*) => {
            return format_js_err!($fmt, $($arg)*);
        };
    }

    /// Unique identifier to resources too expensive to pass between Rust/JS layer.
    pub type Handle = shim::Handle;

    /// Type of target content.
    /// Should map to Source defined in triage library.
    #[wasm_bindgen]
    #[derive(Debug)]
    pub enum Source {
        Inspect,
    }

    /// Object to manage lifetime of objects needed for Triage analysis.
    #[wasm_bindgen]
    pub struct TriageManager {
        #[wasm_bindgen(skip)]
        pub shim: shim::TriageManager,
    }

    #[wasm_bindgen]
    impl TriageManager {
        #[wasm_bindgen(constructor)]
        pub fn new() -> TriageManager {
            TriageManager { shim: shim::TriageManager::new() }
        }

        /// Attempt to build new Context using map of configs.
        /// Returns a Handle that can be passed to `TriageManager::analyze` method.
        ///
        /// # Arguments
        ///
        /// * `configs` - JS map of configs objects to
        ///               forward to triage library.
        #[wasm_bindgen]
        pub fn build_context(&mut self, configs: JsValue) -> Result<Handle, JsValue> {
            let serialized_configs = configs.as_string();
            if serialized_configs.is_none() {
                bail_js_err!("configs param is not String.");
            }

            match json5::from_str::<HashMap<String, String>>(&serialized_configs.unwrap().as_str())
            {
                Err(err) => format_js_err!("Failed to deserialize configs: {}", err),
                Ok(configs) => match self.shim.build_context(configs) {
                    Err(err) => format_js_err!("Failed to parse configs: {}", err),
                    Ok(id) => Ok(id),
                },
            }
        }

        /// Attempt to build new Target.
        /// Returns a Handle that can be passed to `TriageManager::analyze` method.
        ///
        /// # Arguments
        ///
        /// * `source` - Type for target (e.g. Inspect for "inspect.json").
        /// * `name` - Name of target (e.g. filename).
        /// * `content` - Content of target file.
        #[wasm_bindgen]
        pub fn build_target(
            &mut self,
            source: Source,
            name: &str,
            content: &str,
        ) -> Result<Handle, JsValue> {
            let mut build_target = match source {
                Source::Inspect => |name, content| self.shim.build_inspect_target(name, content),
            };

            match build_target(name, content) {
                Ok(id) => Ok(id),
                Err(err) => format_js_err!("Failed to parse Inspect tree: {}", err),
            }
        }

        /// Analyze all DiagnosticData against loaded configs and
        /// generate corresponding ActionResults.
        /// Returns JSON-serialized value of triage library's `analyze` function's return value.
        ///
        /// # Arguments
        ///
        /// * `targets` - Handles for targets.
        /// * `context` - Handle for context.
        #[wasm_bindgen]
        pub fn analyze(&mut self, targets: &[Handle], context: Handle) -> Result<JsValue, JsValue> {
            match self.shim.analyze(&targets, context) {
                Ok(results) => Ok(JsValue::from_str(&results)),
                Err(err) => format_js_err!("Failed to run analysis: {}", err),
            }
        }
    }
}
