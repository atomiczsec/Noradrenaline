# cloud_metadata_check

Probes the local Instance Metadata Service at `169.254.169.254` from inside the agent process. Reports cloud provider, AWS IMDS mode, reachable identity material as bounded snippets by default, and instance context fields. Pass `presence` to suppress credential and token snippets.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `cloud_metadata_check` | `[presence]` | Probes AWS, Azure, and GCP metadata endpoints. `presence` reports provider and context without credential or token snippets. |

## Example Output

```text
[+] cloud_metadata_check started
[i] imds_reachable: yes
[i] provider: aws
[i] imds_mode: v2
[i] iam_role: MyInstanceRole
[i] identity_available: yes
[i] access_key_id: ASIAEXAMPLE1234567
[i] secret_key_snip: wJalrXUtnFEMI/K7MDENG
[i] token_snip: IQoJb3JpZ2luX2VjEPT/nd
[i] instance_id: i-0abc123def456
[i] region: us-east-1
[+] cloud_metadata_check complete
```

Non-cloud or blocked IMDS:

```text
[+] cloud_metadata_check started
[i] imds_reachable: no
[+] cloud_metadata_check complete
```
