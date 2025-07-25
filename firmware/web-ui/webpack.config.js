const path = require('path');
const TerserPlugin = require('terser-webpack-plugin');
const HtmlWebpackPlugin = require('html-webpack-plugin');
const CopyPlugin = require('copy-webpack-plugin');
const CompressionPlugin = require('compression-webpack-plugin');

module.exports = {
  mode: 'production',
  entry: {
    main: './src/main.js', // Entry point for main page
    plot: './plot.js',       // Entry point for plot page
  },
  output: {
    filename: '[name].js',
    path: path.resolve(__dirname, '../src/data'),
    clean: true, // Clean the output directory before emit.
  },
  plugins: [
    new HtmlWebpackPlugin({
      template: './src/index.html',
      filename: 'index.html',
      chunks: ['main'], // Include only main entry point's JS
      inject: 'body',
    }),
    new HtmlWebpackPlugin({
      template: './src/plot.html',
      filename: 'plot.html',
      chunks: ['plot'], // Include only plot entry point's JS
      inject: 'body',
    }),
    new CopyPlugin({
      patterns: [
        { from: 'src/assets', to: 'assets' },
      ],
    }),
    new CompressionPlugin({
      test: /\.(js|html|css|svg|webp)$/,
      filename: '[path][base].gz',
      algorithm: 'gzip',
      threshold: 1024,
      minRatio: 0.8,
      deleteOriginalAssets: true,
    }),
  ],
};
