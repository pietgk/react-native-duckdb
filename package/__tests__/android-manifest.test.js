const fs = require('fs');
const path = require('path');

// AGP 7.3+ builds with AndroidManifestNew.xml, not AndroidManifest.xml.
describe('Android manifest', () => {
  it('registers the provider that sets the native paths', () => {
    const manifest = fs.readFileSync(
      path.resolve(__dirname, '../android/src/main/AndroidManifestNew.xml'),
      'utf8'
    );
    expect(manifest).toContain('com.rnduckdb.RNDuckDBInitProvider');
  });
});
