document.addEventListener('DOMContentLoaded', () => {
    fetchStats();
    initRevealAnimations();
    initParallax();
});

async function fetchStats() {
    const statsContainer = document.getElementById('release-list');
    const totalDownloadsEl = document.getElementById('total-downloads');

    try {
        const response = await fetch('https://api.github.com/repos/thedevil4k/FTORRENT/releases');
        if (!response.ok) throw new Error('GitHub API rate limit or error');
        const releases = await response.json();

        let totalDownloads = 0;
        statsContainer.innerHTML = '';

        releases.forEach((release, index) => {
            let releaseDownloads = 0;
            release.assets.forEach(asset => {
                releaseDownloads += asset.download_count;
            });
            totalDownloads += releaseDownloads;

            const row = document.createElement('div');
            row.className = `release-row reveal reveal-delay-${(index % 3) + 1}`;
            row.innerHTML = `
                <div class="release-info">
                    <h4>Version ${release.name || release.tag_name}</h4>
                    <span>Released on ${new Date(release.published_at).toLocaleDateString()}</span>
                </div>
                <div class="download-pill">
                    <i class="fas fa-download"></i> ${releaseDownloads.toLocaleString()} Downloads
                </div>
            `;
            statsContainer.appendChild(row);
        });

        if (totalDownloadsEl) {
            animateValue(totalDownloadsEl, 0, totalDownloads, 1500);
        }

        // Re-run reveal to catch new elements
        setTimeout(initRevealAnimations, 100);

    } catch (error) {
        console.error('Error fetching stats:', error);
        statsContainer.innerHTML = '<p style="color: var(--accent)">Error loading statistics.</p>';
    }
}

function initRevealAnimations() {
    const reveals = document.querySelectorAll('.reveal');
    const observer = new IntersectionObserver((entries) => {
        entries.forEach(entry => {
            if (entry.isIntersecting) {
                entry.target.classList.add('active');
            }
        });
    }, { threshold: 0.1 });

    reveals.forEach(reveal => observer.observe(reveal));
}

function initParallax() {
    const blobs = document.querySelectorAll('.bg-blob');
    document.addEventListener('mousemove', (e) => {
        const { clientX, clientY } = e;
        const x = (clientX / window.innerWidth - 0.5) * 40;
        const y = (clientY / window.innerHeight - 0.5) * 40;

        blobs.forEach((blob, index) => {
            const factor = (index + 1) * 0.5;
            blob.style.transform = `translate(${x * factor}px, ${y * factor}px)`;
        });
    });
}

function animateValue(obj, start, end, duration) {
    let startTimestamp = null;
    const step = (timestamp) => {
        if (!startTimestamp) startTimestamp = timestamp;
        const progress = Math.min((timestamp - startTimestamp) / duration, 1);
        obj.innerHTML = Math.floor(progress * (end - start) + start).toLocaleString();
        if (progress < 1) {
            window.requestAnimationFrame(step);
        }
    };
    window.requestAnimationFrame(step);
}

