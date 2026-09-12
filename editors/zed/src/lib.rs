use zed_extension_api::{self as zed, settings::LspSettings, LanguageServerId, Result};

struct Rivel;

impl zed::Extension for Rivel {
    fn new() -> Self {
        Self
    }

    fn language_server_command(
        &mut self,
        language_server_id: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<zed::Command> {
        let settings = LspSettings::for_worktree(language_server_id.as_ref(), worktree)?;
        if let Some(binary) = settings.binary {
            if let Some(path) = binary.path {
                return Ok(zed::Command {
                    command: path,
                    args: binary.arguments.unwrap_or_else(|| vec!["--stdio".into()]),
                    env: binary.env.unwrap_or_default().into_iter().collect(),
                });
            }
        }
        let path = worktree.which("rivel-lsp").ok_or_else(|| {
            "rivel-lsp was not found. Run `make lsp` in the Rivel checkout, then add its bin directory to PATH or set lsp.rivel.binary.path in Zed settings.".to_string()
        })?;
        Ok(zed::Command {
            command: path,
            args: vec!["--stdio".into()],
            env: vec![],
        })
    }
}

zed::register_extension!(Rivel);
