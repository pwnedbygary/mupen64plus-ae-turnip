# TODO

- [ ] **Turnip driver support for remaining rendering plugins** — the custom Vulkan driver (AdrenoTools) currently only works with the Parallel (ParaLLEl-RDP) plugin. Add Vulkan-backed rendering to the other video plugins (e.g. GLideN64's Vulkan backend) and wire them into the same `setCustomVulkanDriver` hookup so the Turnip driver applies across all cores.