document.addEventListener('DOMContentLoaded', () => {
    fetchStats();
});

async function fetchStats() {
    const statsContainer = document.getElementById('release-list');
    const totalDownloadsEl = document.getElementById('total-downloads');

    try {
        const response = await fetch('https://api.github.com/repos/thedevil4k/FTORRENT/releases');
        const releases = await response.json();

        let totalDownloads = 0;
        statsContainer.innerHTML = '';

        releases.forEach(release => {
            let releaseDownloads = 0;
            release.assets.forEach(asset => {
                releaseDownloads += asset.download_count;
            });
            totalDownloads += releaseDownloads;

            const row = document.createElement('div');
            row.className = 'release-row';
            row.innerHTML = `
                <div class="release-info">
                    <h4>Version ${release.name || release.tag_name}</h4>
                    <span>Released on ${new Date(release.published_at).toLocaleDateString()}</span>
                </div>
                <div class="download-pill">
                    <i class="fas fa-download"></i> ${releaseDownloads} Downloads
                </div>
            `;
            statsContainer.appendChild(row);
        });

        if (totalDownloadsEl) {
            totalDownloadsEl.innerText = totalDownloads;
        }
    } catch (error) {
        console.error('Error fetching stats:', error);
        statsContainer.innerHTML = '<p style="color: var(--accent)">Error loading statistics.</p>';
    }
}
