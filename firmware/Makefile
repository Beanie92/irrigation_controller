.PHONY: web-assets

# This target builds the web UI assets.
# It installs npm dependencies, bundles the javascript,
# and converts the bundle to a C++ header file.
web-assets:
	@echo "Building web assets with Webpack..."
	@(cd web-ui && npm install)
	@(cd web-ui && npm run build)
	@echo "Web assets built successfully."
