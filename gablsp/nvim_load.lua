vim.api.nvim_create_autocmd("FileType", {
  pattern = "gab",
  callback = function()
    vim.lsp.start {
      name = "gablsp",
      cmd = { "/home/tr/repos/cgab/gablsp.exe" },
      on_attach = function(server)
        vim.notify ("attatched")
      end,
    }
  end,
})
